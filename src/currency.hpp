#pragma once
#include "model.hpp"
#include "worker.hpp"
#include <QByteArray>
#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QString>
#include <functional>
#include <optional>
#include <stop_token>
namespace palette {
struct CurrencyQuery { double amount=1; QString from,to; };
struct CurrencyRates { QDate date; QHash<QString,double> usd; };
std::optional<CurrencyQuery> parse_currency_query(QString text);
std::optional<CurrencyRates> parse_currency_rates(QByteArray bytes,QDate today=QDate::currentDate());
CalcResult convert_currency(const CurrencyQuery&,const CurrencyRates&);
QByteArray download_currency_rates(std::stop_token stop={});
class CurrencyService : public QObject {
public:
    using Downloader=std::function<QByteArray(std::stop_token)>;
    explicit CurrencyService(QObject* parent=nullptr,std::function<void()> changed={},QString cache_path={},Downloader downloader=download_currency_rates);
    ~CurrencyService() override;
    void request();
    CalcResult convert(const CurrencyQuery&) const;
    QString detail() const;
private:
    std::function<void()> changed_;
    QString cache_path_;
    Downloader downloader_;
    std::optional<CurrencyRates> rates_;
    QDateTime next_attempt_;
    bool loading_=false, cache_loaded_=false, cached_=true;
    Worker worker_;
};
}
