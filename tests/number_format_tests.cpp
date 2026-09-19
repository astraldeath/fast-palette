#include "number_format.hpp"
#include "core.hpp"
#include <iostream>
int main(){int failed=0;const auto check=[&](bool ok,const char* name){if(!ok){++failed;std::cerr<<"FAIL: "<<name<<'\n';}};
    using namespace palette;const QLocale en("en_US"),de("de_DE"),fr("fr_FR"),hi("hi_IN");
    check(localized_number_result("1234567.89",en)=="1,234,567.89","US grouping");
    check(localized_number_result("1234567.89",de)=="1.234.567,89","German grouping and decimal");
    check(localized_number_result("1234567.89",fr)=="1"+fr.groupSeparator()+"234"+fr.groupSeparator()+"567,89","French spacing");
    check(localized_number_result("1234567.89",hi)=="12,34,567.89","Indian grouping");
    check(localized_number_result("1e20",en)=="100,000,000,000,000,000,000","large exponent avoids integer overflow");
    check(localized_number_result("-1.25e-6",de)=="-0,00000125","small negative exponent");
    check(plain_number_result("1e6")=="1000000","copy expands without grouping");
    check(plain_number_result(".25")=="0.25","plain decimal has leading zero");
    check(localized_number_result("2000 MB",de)=="2.000 MB","conversion units retained");
    check(localized_number_result("5 ft 10.87 in",de)=="5 ft 10,87 in","mixed measurement values formatted independently");
    check(plain_number_result("5e-324")=="0."+QString(323,'0')+"5","smallest subnormal expands exactly");
    check(plain_number_result("1.234e2")=="123.4","decimal moves within mantissa");
    check(localized_number_result("1000",QLocale("es_ES"))==QLocale("es_ES").toString(1000),"locale minimum grouping respected");
    for(auto input:{L"100!",L"1e308",L"1e-300",L"1/3"}){
        const auto value=calculate(input);const auto plain=plain_number_result(QString::fromStdWString(value.text));bool ok=false;
        check(plain.toDouble(&ok)==value.value && ok && !plain.contains('e',Qt::CaseInsensitive),"expanded decimal round trips without changing value");
    }
    check(normalize_number_input("100,000.25 usd to yen",en)=="100000.25 usd to yen","grouped currency input");
    check(normalize_number_input("100.000,25 + 1",de)=="100000.25 + 1","localized input");
    check(normalize_number_input("100,00",en)=="100,00","malformed grouping remains invalid");
    check(normalize_number_input("1,000,000 + 2,000",en)=="1000000 + 2000","multiple grouped numbers");
    check(normalize_number_input("1.25e-6",en)=="1.25e-6","scientific input preserved");
    return failed?1:0;
}
