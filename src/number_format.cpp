#include "number_format.hpp"
#include <QRegularExpression>
namespace palette {
namespace {
QString expand(QString number) {
    QString sign;
    if(number.startsWith('-')){sign="-";number.remove(0,1);}
    const auto exponent_at=number.indexOf('e',0,Qt::CaseInsensitive);
    int exponent=0;
    if(exponent_at>=0){bool valid=false;exponent=number.mid(exponent_at+1).toInt(&valid);
        if(!valid || exponent < -400 || exponent > 400)return sign+number;
        number.truncate(exponent_at);
    }
    const auto dot=number.indexOf('.');
    const auto decimal=(dot<0?number.size():dot)+exponent;
    number.remove('.');
    if(decimal<=0)number="0."+QString(-decimal,'0')+number;
    else if(decimal>=number.size())number+=QString(decimal-number.size(),'0');
    else number.insert(decimal,'.');
    return sign+number;
}
template<class Format> QString map_numbers(const QString& result,Format format) {
    static const QRegularExpression number("-?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][+-]?[0-9]+)?");
    QString output;qsizetype position=0;
    auto matches=number.globalMatch(result);
    while(matches.hasNext()){const auto match=matches.next();output+=result.mid(position,match.capturedStart()-position);
        output+=format(expand(match.captured()));position=match.capturedEnd();}
    return output+result.mid(position);
}
QString localize(QString number,const QLocale& locale) {
    QString sign;if(number.startsWith('-')){sign=locale.negativeSign();number.remove(0,1);}
    const auto decimal=number.indexOf('.');
    auto whole=decimal<0?number:number.left(decimal);
    const auto fraction=decimal<0?QString{}:number.mid(decimal+1);
    const auto separator=locale.groupSeparator();
    if(!separator.isEmpty()){
        const auto groups=locale.toString(123456789LL).split(separator);
        if(groups.size()>1){
            const auto primary=groups.back().toUcs4().size();
            const auto secondary=groups.size()>2?groups[groups.size()-2].toUcs4().size():primary;
            int minimum=4;
            for(qlonglong probe=1000;minimum<9 && !locale.toString(probe).contains(separator);probe*=10)++minimum;
            if(primary>0 && secondary>0 && whole.size()>=minimum)
                for(auto at=whole.size()-primary;at>0;at-=secondary)whole.insert(at,separator);
        }
    }
    auto output=whole+(decimal<0?QString{}:locale.decimalPoint()+fraction);
    const auto zero=locale.zeroDigit().toUcs4();
    if(zero.size()==1 && zero[0]!=U'0'){
        QString digits;for(const auto character:output){if(character>='0' && character<='9'){
            const char32_t localized=zero[0]+character.unicode()-'0';digits+=QString::fromUcs4(&localized,1);
        }else digits+=character;}output=digits;
    }
    return sign+output;
}
}
QString plain_number_result(const QString& result){return map_numbers(result,[](const QString& number){return number;});}
QString localized_number_result(const QString& result,const QLocale& locale){return map_numbers(result,[&](const QString& number){return localize(number,locale);});}
}
