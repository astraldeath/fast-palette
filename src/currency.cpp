#include "currency.hpp"
#include "number_format.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <windows.h>
#include <winhttp.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>

namespace palette {
namespace {
constexpr qsizetype max_payload=1024*1024;
QString currency_code(QString name){
    name=name.simplified().toLower();
    static const QHash<QString,QString> aliases={
        {"dollar","usd"},{"dollars","usd"},{"us dollar","usd"},{"us dollars","usd"},{"united states dollar","usd"},
        {"euro","eur"},{"euros","eur"},{"yen","jpy"},{"japanese yen","jpy"},
        {"won","krw"},{"korean won","krw"},{"south korean won","krw"},
        {"bitcoin","btc"},{"bitcoins","btc"},{"monero","xmr"},{"ethereum","eth"},{"ether","eth"},
        {"pound","gbp"},{"pounds","gbp"},{"british pound","gbp"},{"british pounds","gbp"},{"pound sterling","gbp"},
        {"canadian dollar","cad"},{"canadian dollars","cad"},{"australian dollar","aud"},{"australian dollars","aud"},
        {"swiss franc","chf"},{"swiss francs","chf"},{"yuan","cny"},{"chinese yuan","cny"},{"renminbi","cny"},
        {"rupee","inr"},{"rupees","inr"},{"indian rupee","inr"},{"indian rupees","inr"}
    };
    if(auto i=aliases.constFind(name);i!=aliases.cend())return *i;
    static const QRegularExpression code("^[a-z0-9]{2,12}$");
    return code.match(name).hasMatch()?name:QString{};
}
bool usable_date(QDate date,QDate today){return date.isValid()&&today.isValid()&&date>=today.addDays(-7)&&date<=today.addDays(1);}
struct InternetHandle {HINTERNET value=nullptr;~InternetHandle(){if(value)WinHttpCloseHandle(value);}};
}
std::optional<CurrencyQuery> parse_currency_query(QString text){
    if(text.size()>512)return {};
    text=normalize_number_input(text).simplified().toLower();
    static const QRegularExpression syntax("^(?:([+-]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:e[+-]?[0-9]+)?)\\s*)?([a-z][a-z0-9 ]*?)\\s+to\\s+([a-z][a-z0-9 ]*)$");
    const auto match=syntax.match(text);if(!match.hasMatch())return {};
    CurrencyQuery query;query.from=currency_code(match.captured(2));query.to=currency_code(match.captured(3));
    if(query.from.isEmpty()||query.to.isEmpty())return {};
    if(!match.captured(1).isEmpty()){bool ok=false;query.amount=match.captured(1).toDouble(&ok);if(!ok||!std::isfinite(query.amount))return {};}
    return query;
}
std::optional<CurrencyRates> parse_currency_rates(QByteArray bytes,QDate today){
    if(bytes.isEmpty()||bytes.size()>max_payload)return {};
    QJsonParseError error;const auto document=QJsonDocument::fromJson(bytes,&error);
    if(error.error!=QJsonParseError::NoError||!document.isObject())return {};
    const auto root=document.object();const auto dateText=root.value("date").toString();
    CurrencyRates result;result.date=QDate::fromString(dateText,Qt::ISODate);
    if(dateText.size()!=10||!usable_date(result.date,today)||result.date.toString(Qt::ISODate)!=dateText||!root.value("usd").isObject())return {};
    const auto rates=root.value("usd").toObject();if(rates.size()<2||rates.size()>10000)return {};
    static const QRegularExpression code("^[a-z0-9]{2,12}$");
    for(auto i=rates.begin();i!=rates.end();++i){
        if(!code.match(i.key()).hasMatch()||!i.value().isDouble())return {};
        const double value=i.value().toDouble();if(!std::isfinite(value)||value<=0)return {};
        result.usd.insert(i.key(),value);
    }
    if(result.usd.value("usd")!=1)return {};
    return result;
}
CalcResult convert_currency(const CurrencyQuery& query,const CurrencyRates& rates){
    const auto from=rates.usd.constFind(query.from.toLower()),to=rates.usd.constFind(query.to.toLower());
    if(from==rates.usd.cend()||to==rates.usd.cend())return {CalcStatus::error,0,L"Unsupported currency"};
    if(!std::isfinite(query.amount)||!std::isfinite(*from)||!std::isfinite(*to)||*from<=0||*to<=0)return {CalcStatus::error,0,L"Invalid currency amount or rate"};
    const double value=query.amount / *from * *to;
    if(!std::isfinite(value))return {CalcStatus::error,0,L"Currency result is out of range"};
    auto formatted=std::abs(value)>0 && std::abs(value)<0.0001?QString::number(value,'g',4):QString::number(value==0?0:value,'f',4);
    if(!formatted.contains('e') && formatted.contains('.')){while(formatted.endsWith('0'))formatted.chop(1);if(formatted.endsWith('.'))formatted.chop(1);}
    return {CalcStatus::value,value,(formatted+" "+query.to.toUpper()).toStdWString()};
}
QByteArray download_currency_rates(std::stop_token stop){
    if(stop.stop_requested())return {};
    InternetHandle session{WinHttpOpen(L"FastPalette-Currency/1",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0)};
    if(!session.value)return {};
    if(!WinHttpSetTimeouts(session.value,5000,5000,5000,5000))return {};
    // Closing the active request interrupts synchronous WinHTTP I/O at the total deadline.
    std::atomic<HINTERNET> active{nullptr};
    std::atomic<bool> cancelled{false};
    auto cancel=[&]{cancelled=true;if(auto handle=active.exchange(nullptr))WinHttpCloseHandle(handle);};
    std::stop_callback stopCallback(stop,cancel);
    std::mutex timerMutex;std::condition_variable_any timer;
    std::jthread watchdog([&](std::stop_token token){std::unique_lock lock(timerMutex);timer.wait_for(lock,token,std::chrono::seconds(15),[]{return false;});if(!token.stop_requested())cancel();});
    struct Finish {std::jthread& thread;std::condition_variable_any& timer;std::atomic<HINTERNET>& active;~Finish(){thread.request_stop();timer.notify_all();thread.join();if(auto handle=active.exchange(nullptr))WinHttpCloseHandle(handle);}} finish{watchdog,timer,active};
    const wchar_t* hosts[]={L"cdn.jsdelivr.net",L"latest.currency-api.pages.dev"};
    const wchar_t* paths[]={L"/npm/@fawazahmed0/currency-api@latest/v1/currencies/usd.min.json",L"/v1/currencies/usd.min.json"};
    for(int endpoint=0;endpoint<2&&!cancelled.load();++endpoint){
        InternetHandle connection{WinHttpConnect(session.value,hosts[endpoint],INTERNET_DEFAULT_HTTPS_PORT,0)};if(!connection.value)continue;
        const auto request=WinHttpOpenRequest(connection.value,L"GET",paths[endpoint],nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE);
        if(!request)continue;
        active=request;
        if(cancelled.load()){if(auto handle=active.exchange(nullptr))WinHttpCloseHandle(handle);break;}
        // Fixed endpoints only: do not follow redirects to any other host or scheme.
        DWORD redirects=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        bool ok=WinHttpSetOption(request,WINHTTP_OPTION_REDIRECT_POLICY,&redirects,sizeof(redirects))&&
            WinHttpSendRequest(request,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0)&&WinHttpReceiveResponse(request,nullptr);
        DWORD status=0,size=sizeof(status);
        ok=ok&&WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX)&&status==200;
        QByteArray bytes;char buffer[16384];
        while(ok&&!cancelled.load()){
            DWORD read=0;if(!WinHttpReadData(request,buffer,sizeof(buffer),&read)){ok=false;break;}
            if(!read)break;
            if(bytes.size()+read>max_payload){ok=false;break;}
            bytes.append(buffer,static_cast<qsizetype>(read));
        }
        if(auto handle=active.exchange(nullptr))WinHttpCloseHandle(handle);
        if(ok&&!cancelled.load()&&parse_currency_rates(bytes))return bytes;
    }
    return {};
}
CurrencyService::CurrencyService(QObject* parent,std::function<void()> changed,QString path,Downloader downloader)
    :QObject(parent),changed_(std::move(changed)),cache_path_(std::move(path)),downloader_(std::move(downloader)){
    if(cache_path_.isEmpty())cache_path_=QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)+"/FastPalette/currency-rates.json";
}
CurrencyService::~CurrencyService(){worker_.stop();}
void CurrencyService::request(){
    const auto now=QDateTime::currentDateTimeUtc();
    if(loading_||(next_attempt_.isValid()&&now<next_attempt_))return;
    loading_=true;
    const bool loadCache=!cache_loaded_;cache_loaded_=true;
    const auto path=cache_path_;const auto downloader=downloader_;
    worker_.enqueue([this,path,downloader,loadCache](std::stop_token stop){
        if(loadCache){QFile file(path);if(file.open(QIODevice::ReadOnly)&&file.size()<=max_payload){
            if(auto cached=parse_currency_rates(file.read(max_payload+1))){
                QMetaObject::invokeMethod(this,[this,cached=std::move(*cached)]{rates_=cached;cached_=true;if(changed_)changed_();},Qt::QueuedConnection);
            }
        }}
        if(stop.stop_requested())return;
        QByteArray bytes;try{if(downloader)bytes=downloader(stop);}catch(...){bytes.clear();}
        auto rates=parse_currency_rates(bytes);
        if(stop.stop_requested())return;
        if(rates){QDir().mkpath(QFileInfo(path).absolutePath());QSaveFile file(path);if(file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size())file.commit();}
        QMetaObject::invokeMethod(this,[this,rates=std::move(rates)]{
            loading_=false;
            const auto now=QDateTime::currentDateTimeUtc();
            next_attempt_=now.addSecs(rates?6*60*60:60);
            if(rates){rates_=rates;cached_=false;const auto midnight=QDateTime(now.date().addDays(1),QTime(0,0),QTimeZone::UTC);if(midnight<next_attempt_)next_attempt_=midnight;}
            else cached_=true;
            if(changed_)changed_();
        },Qt::QueuedConnection);
    });
}
CalcResult CurrencyService::convert(const CurrencyQuery& query)const{
    if(rates_&&usable_date(rates_->date,QDate::currentDate()))return convert_currency(query,*rates_);
    return {CalcStatus::incomplete,0,loading_?L"Fetching currency rates…":L"Currency rates unavailable"};
}
QString CurrencyService::detail()const{
    if(rates_&&usable_date(rates_->date,QDate::currentDate()))return QString(cached_||rates_->date<QDate::currentDate()?"Cached rates · ":"Daily rates · ")+rates_->date.toString(Qt::ISODate);
    return loading_?"Fetching currency rates…":"Currency rates unavailable";
}
}


