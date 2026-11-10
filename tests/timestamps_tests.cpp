#include "timestamps.hpp"
#include <QCoreApplication>
#include <QTimeZone>
#include <iostream>
using namespace palette;
int main(int argc,char** argv){QCoreApplication application(argc,argv);int failed=0;
    const auto check=[&](bool ok,const char* name){if(!ok){++failed;std::cerr<<"FAIL: "<<name<<'\n';}};
    const auto value=[&](QString query,QString expected){const auto result=convert_timestamp(query);check(result.status==CalcStatus::value && result.text==expected,qPrintable(query));};
    value("unix 0","1970-01-01T00:00:00Z");
    value("1700000000 to date","2023-11-14T22:13:20Z");
    value("unix 1700000000123","2023-11-14T22:13:20.123Z");
    value("unix 1000ms","1970-01-01T00:00:01Z");
    value("unix -1","1969-12-31T23:59:59Z");
    value("unix -1ms","1969-12-31T23:59:59.999Z");
    value("1970-01-01 to unix","0");
    value("1970-01-01T01:00:00+01:00 to unix","0");
    value("1970-01-01T00:00:00.123Z to unix ms","123");
    value("unix 2000-02-29","951782400");
    value("1970-01-01 00:00:01 to unix","1");
    value("unix 253402300799s","9999-12-31T23:59:59Z");
    value("unix -62135596800s","0001-01-01T00:00:00Z");
    const auto now=QDateTime::fromMSecsSinceEpoch(1700000000123LL,QTimeZone::UTC);
    check(convert_timestamp("now to unix",now).text=="1700000000","deterministic now seconds");
    check(convert_timestamp("now to unix ms",now).text=="1700000000123","deterministic now milliseconds");
    const auto local=convert_timestamp("1700000000 to local");
    check(local.status==CalcStatus::value && local.detail=="Local time" && QDateTime::fromString(local.text,Qt::ISODate).toSecsSinceEpoch()==1700000000LL,"local time preserves instant");
    for(auto query:{"unix 2023-02-29","unix 99999999999999999999999","unix 253402300800s","unix 1.5","unix bad"})check(convert_timestamp(query).status==CalcStatus::error,query);
    check(convert_timestamp("unix").status==CalcStatus::incomplete,"prefix prompts for input");
    for(auto query:{"1700000000","2+2","2gb to mb","5 btc to usd","notepad"})check(convert_timestamp(query).status==CalcStatus::none,"ordinary queries untouched");
    return failed?1:0;
}
