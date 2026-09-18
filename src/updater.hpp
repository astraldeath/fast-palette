#pragma once
#include "worker.hpp"
#include <QObject>
#include <QTimer>
#include <QString>
#include <QByteArray>
#include <functional>
#include <optional>
class QWidget;
namespace palette {
struct UpdateRelease { QString version,executable,checksum; };
bool newer_version(const QString& candidate,const QString& current);
std::optional<UpdateRelease> parse_update_release(const QByteArray& json,const QString& current);
bool verify_update(const QByteArray& binary,const QByteArray& checksum);
QByteArray download_update_url(const QString& url,qsizetype limit,std::stop_token stop={});
std::optional<int> run_update_helper();
void clean_update_directory(const QString& directory);
class Updater final : public QObject {
public:
    Updater(QWidget* owner,std::function<bool()> idle);
    ~Updater() override;
    void set_automatic(bool enabled);
    void check(bool manual=true,std::function<void()> completed={});
private:
    void install(bool manual);
    QWidget* owner_;
    std::function<bool()> idle_;
    Worker worker_;
    QTimer check_timer_,install_timer_;
    bool automatic_=false,busy_=false;
    QString directory_,version_,hash_;
};
}
