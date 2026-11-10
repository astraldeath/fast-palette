#include "timestamps.hpp"
#include "number_format.hpp"
#include <QRegularExpression>
#include <QTimeZone>
namespace palette {
TimestampResult convert_timestamp(QString query,QDateTime now){
    if(query.size()>512)return {};
    query=query.trimmed();
    static const QRegularExpression prefix("^(unix|epoch|timestamp)(?:\\s+|$)",QRegularExpression::CaseInsensitiveOption);
    const auto head=prefix.match(query);const bool prefixed=head.hasMatch();
    if(prefixed)query.remove(0,head.capturedLength());
    static const QRegularExpression suffix("\\s+to\\s+(unix(?:\\s+ms)?|timestamp|epoch|date|utc|local)$",QRegularExpression::CaseInsensitiveOption);
    const auto tail=suffix.match(query);QString target;
    if(tail.hasMatch()){target=tail.captured(1).simplified().toLower();query.truncate(tail.capturedStart());}
    if(!prefixed && target.isEmpty())return {};
    query=query.trimmed();
    if(query.isEmpty())return {CalcStatus::incomplete,"Enter a timestamp or ISO date",{}};
    const bool output_unix=target=="unix" || target=="unix ms" || target=="timestamp" || target=="epoch";
    const bool local=target=="local";
    auto invalid=[](){return TimestampResult{CalcStatus::error,"Invalid timestamp or ISO date",{}};};
    if(query.compare("now",Qt::CaseInsensitive)==0){
        if(!now.isValid())return invalid();
        if(target.isEmpty() || output_unix){const bool ms=target=="unix ms";return {CalcStatus::value,QString::number(ms?now.toMSecsSinceEpoch():now.toSecsSinceEpoch()),ms?"Unix milliseconds":"Unix seconds"};}
        auto date=local?now.toLocalTime():now.toUTC();
        return {CalcStatus::value,date.toString(Qt::ISODate),local?"Local time":"UTC"};
    }
    static const QRegularExpression integer("^([+-]?[0-9]+)\\s*(ms|s)?$",QRegularExpression::CaseInsensitiveOption);
    const auto numeric=integer.match(normalize_number_input(query));
    if(numeric.hasMatch()){
        bool ok=false;const auto number=numeric.captured(1).toLongLong(&ok);if(!ok)return invalid();
        const auto unit=numeric.captured(2).toLower();
        const bool ms=unit=="ms" || (unit.isEmpty() && (number>=100000000000LL || number<=-100000000000LL));
        constexpr qint64 min_ms=-62135596800000LL,max_ms=253402300799999LL;
        if(ms?(number<min_ms || number>max_ms):(number<min_ms/1000 || number>max_ms/1000))return {CalcStatus::error,"Timestamp is outside years 1-9999",{}};
        const qint64 millis=ms?number:number*1000;
        if(output_unix){const bool out_ms=target=="unix ms";return {CalcStatus::value,QString::number(out_ms?millis:QDateTime::fromMSecsSinceEpoch(millis,QTimeZone::UTC).toSecsSinceEpoch()),out_ms?"Unix milliseconds":"Unix seconds"};}
        auto date=QDateTime::fromMSecsSinceEpoch(millis,QTimeZone::UTC);if(local)date=date.toLocalTime();
        if(date.date().year()<1 || date.date().year()>9999)return invalid();
        return {CalcStatus::value,date.toString(millis%1000?Qt::ISODateWithMs:Qt::ISODate),local?"Local time":"UTC"};
    }
    if(!target.isEmpty() && !output_unix && !prefixed)return {};
    QDateTime date;
    static const QRegularExpression iso_date("^[0-9]{4}-[0-9]{2}-[0-9]{2}$");
    static const QRegularExpression iso_time("^[0-9]{4}-[0-9]{2}-[0-9]{2}[Tt ][0-9]{2}:[0-9]{2}(?::[0-9]{2}(?:\\.[0-9]{1,3})?)?(?:[Zz]|[+-][0-9]{2}:[0-9]{2})?$");
    if(iso_date.match(query).hasMatch())date=QDateTime(QDate::fromString(query,Qt::ISODate),QTime(0,0),QTimeZone::UTC);
    else if(iso_time.match(query).hasMatch()){
        query[10]='T';
        static const QRegularExpression zone("([Zz]|[+-][0-9]{2}:[0-9]{2})$");
        if(!zone.match(query).hasMatch())query+='Z';else if(query.endsWith('z'))query[query.size()-1]='Z';
        date=QDateTime::fromString(query,Qt::ISODateWithMs);
    }
    if(!date.isValid() || date.date().year()<1 || date.date().year()>9999)return invalid();
    if(local){date=date.toLocalTime();return {CalcStatus::value,date.toString(Qt::ISODateWithMs),"Local time"};}
    if(target=="date" || target=="utc")return {CalcStatus::value,date.toUTC().toString(Qt::ISODateWithMs),"UTC"};
    const bool ms=target=="unix ms";
    return {CalcStatus::value,QString::number(ms?date.toMSecsSinceEpoch():date.toSecsSinceEpoch()),ms?"Unix milliseconds":"Unix seconds"};
}
}
