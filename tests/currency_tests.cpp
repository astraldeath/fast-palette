#include "currency.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>
#include <atomic>
#include <cmath>
#include <iostream>
#include <limits>
using namespace palette;
static QByteArray payload(QDate date) { return "{\"date\":\""+date.toString(Qt::ISODate).toUtf8()+"\",\"usd\":{\"usd\":1,\"eur\":0.9,\"jpy\":150,\"krw\":1300,\"btc\":0.00002,\"xmr\":0.005}}"; }
int main(int argc,char**argv){
 QCoreApplication app(argc,argv);int failures=0;
 auto check=[&](bool condition,const char* name){if(!condition){std::cerr<<"FAIL: "<<name<<'\n';++failures;}};
 auto q=parse_currency_query("5btc to XMR");check(q&&q->amount==5&&q->from=="btc"&&q->to=="xmr","compact crypto query");
 q=parse_currency_query("5 OP to usd");check(q&&q->from=="op","two-letter crypto code");
 q=parse_currency_query("USD to Japanese yen");check(q&&q->amount==1&&q->to=="jpy","default amount and fiat alias");
 q=parse_currency_query("100 US dollars to Korean won");check(q&&q->from=="usd"&&q->to=="krw","multiword aliases");
 check(!parse_currency_query("1+2")&&!parse_currency_query("Photoshop")&&!parse_currency_query("1e999 usd to jpy")&&!parse_currency_query(QString(513,'a')),"reject ordinary and invalid input");
 auto rates=parse_currency_rates(payload(QDate::currentDate()));check(rates.has_value(),"valid daily rates");
 if(rates){
 check(convert_currency({637.329006872,"usd","usd"},*rates).text==L"637.329 USD","currency precision is concise");
 check(convert_currency({1.23001,"usd","usd"},*rates).text==L"1.23 USD","trailing zeroes removed");
 check(convert_currency({0.000000012345,"usd","usd"},*rates).value>0 && convert_currency({0.000000012345,"usd","usd"},*rates).text!=L"0 USD","tiny values preserved");
 auto value=convert_currency({5,"btc","xmr"},*rates);check(value.status==CalcStatus::value&&std::abs(value.value-1250)<1e-8&&value.text==L"1250 XMR","cross crypto conversion");check(convert_currency({100,"usd","jpy"},*rates).value==15000,"fiat conversion");check(convert_currency({1,"zzz","usd"},*rates).status==CalcStatus::error,"unsupported code");check(convert_currency({std::numeric_limits<double>::infinity(),"usd","jpy"},*rates).status==CalcStatus::error,"infinite amount");check(convert_currency({1e308,"usd","jpy"},*rates).status==CalcStatus::error,"overflow output");}
 check(!parse_currency_rates("{}")&&!parse_currency_rates("no json")&&!parse_currency_rates(payload(QDate::currentDate().addDays(-8)))&&!parse_currency_rates(payload(QDate::currentDate().addDays(2))),"reject malformed and expired dates");
 auto shortCode=payload(QDate::currentDate());shortCode.replace("\"eur\":0.9","\"op\":0.9");check(parse_currency_rates(shortCode).has_value(),"valid feed includes two-letter assets");
 auto bad=payload(QDate::currentDate());bad.replace("\"usd\":1,","\"usd\":2,");check(!parse_currency_rates(bad),"reject nonunit USD");bad=payload(QDate::currentDate());bad.replace("0.005","-1");check(!parse_currency_rates(bad),"reject negative rates");
 QTemporaryDir temp;const auto path=temp.filePath("rates.json");{QFile f(path);check(f.open(QIODevice::WriteOnly),"write cache fixture");f.write(payload(QDate::currentDate().addDays(-1)));}
 std::atomic<int> downloads=0;bool changed=false;bool gui=true;
 {CurrencyService service(nullptr,[&]{changed=true;gui&=QThread::currentThread()==app.thread();},path,[&](std::stop_token){++downloads;QThread::msleep(60);return QByteArray{};});
 check(downloads==0,"constructor does not download");QElapsedTimer timer;timer.start();service.request();service.request();check(timer.elapsed()<40,"request does not block GUI");
 while(timer.elapsed()<2000&&(downloads==0||service.detail().startsWith("Fetching"))){app.processEvents();QThread::msleep(5);}for(int i=0;i<30;++i){app.processEvents();QThread::msleep(5);}
 check(changed&&gui&&downloads==1,"coalesced request and GUI callback");check(service.convert({1,"usd","jpy"}).value==150,"retained cached conversion on network failure");check(service.detail()=="Cached rates · "+QDate::currentDate().addDays(-1).toString(Qt::ISODate),"cached provenance and date");service.request();check(downloads==1,"failure cooldown");}
 const auto freshPath=temp.filePath("fresh/rates.json");downloads=0;changed=false;
 {CurrencyService service(nullptr,[&]{changed=true;},freshPath,[&](std::stop_token){++downloads;return payload(QDate::currentDate());});
  check(service.convert({1,"usd","jpy"}).status==CalcStatus::incomplete,"missing rates are incomplete");
  service.request();QElapsedTimer wait;wait.start();while(!changed&&wait.elapsed()<2000){app.processEvents();QThread::msleep(5);}
  check(changed&&service.convert({1,"usd","jpy"}).value==150,"fresh network result becomes usable");
  check(service.detail()=="Daily rates · "+QDate::currentDate().toString(Qt::ISODate),"daily provenance and date");
  service.request();check(downloads==1,"success cooldown");
  QFile cache(freshPath);check(cache.open(QIODevice::ReadOnly)&&parse_currency_rates(cache.readAll()).has_value(),"network result persisted atomically");}
 bool lateCallback=false;std::atomic<bool> entered=false;
 {CurrencyService service(nullptr,[&]{lateCallback=true;},temp.filePath("missing.json"),[&](std::stop_token stop){entered=true;while(!stop.stop_requested())QThread::msleep(1);return QByteArray{};});
  service.request();QElapsedTimer wait;wait.start();while(!entered&&wait.elapsed()<2000)QThread::msleep(1);check(entered,"worker starts");}
 app.processEvents();check(!lateCallback,"destroyed service cancels worker and queued callback");
 if(app.arguments().contains("--live")){auto live=parse_currency_rates(download_currency_rates());check(live.has_value(),"live endpoint payload");if(live){for(const auto& pair:{CurrencyQuery{5,"btc","xmr"},CurrencyQuery{100,"usd","jpy"},CurrencyQuery{100,"usd","krw"}}){const auto result=convert_currency(pair,*live);check(result.status==CalcStatus::value&&std::isfinite(result.value)&&result.value>0,"live crypto and fiat conversion");}}if(live)for(auto code:{"btc","xmr","usd","jpy","krw"})check(live->usd.value(code)>0,"live supported code");}
 return failures?1:0;
}




