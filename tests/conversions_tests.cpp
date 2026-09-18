#include "conversions.hpp"
#include <cmath>
#include <iostream>
#include <string>

int main() {
    int failures = 0;
    auto check = [&](bool ok, const wchar_t* query) {
        if (!ok) { std::wcerr << L"FAIL: " << query << L'\n'; ++failures; }
    };
    auto value = [&](const wchar_t* query, double expected, const wchar_t* text) {
        auto result = palette::convert_units(query);
        check(result.status == palette::CalcStatus::value && std::abs(result.value - expected) < 1e-7 && result.text == text, query);
    };
    value(L"180 cm to ftin", 180 / 2.54, L"5 ft 10.87 in");
    value(L"5'11\" to cm", 180.34, L"180.34 cm");
    value(L"5 ft 11 in to cm", 180.34, L"180.34 cm");
    value(L"72 in to feet and inches", 72, L"6 ft 0 in");
    value(L"2gb to mb", 2000, L"2000 MB");
    value(L"2 GiB to MiB", 2048, L"2048 MiB");
    value(L"2 MB/s", 16, L"16 Mbps");
    value(L"2 mb/s", .25, L"0.25 MB/s");
    value(L"2 mbps", .25, L"0.25 MB/s");
    value(L"2 MBps", 16, L"16 Mbps");
    value(L"2 Mbps to MBps", .25, L"0.25 MB/s");
    value(L"32 F to C", 0, L"0 C");
    value(L"1 hour in min", 60, L"60 min");
    value(L"1 km -> m", 1000, L"1000 m");
    value(L"1 kg as g", 1000, L"1000 g");
    value(L"1e-15 m to cm", 1e-13, L"1e-13 cm");
    value(L"-5 ft 11 in to cm", -180.34, L"-180.34 cm");
    value(L"2 GB/s to Mbps", 16000, L"16000 Mbps");
    value(L"2 GiB to MB", 2147.483648, L"2147.483648 MB");
    value(L"71.99999 in to ftin", 71.99999, L"6 ft 0 in");
    for (auto query : {L"calculator", L"2+3", L"123", L"7zip"})
        check(palette::convert_units(query).status == palette::CalcStatus::none, query);
    for (auto query : {L"2 cm to", L"2 cm to fti", L"2 cm t"})
        check(palette::convert_units(query).status == palette::CalcStatus::incomplete, query);
    for (auto query : {L"2 cm to kg", L"1e309 cm to m", L"2 cm to nonsense", L"5ftin", L"5 ftin to cm"})
        check(palette::convert_units(query).status == palette::CalcStatus::error, query);
    check(palette::convert_units(std::wstring(513, L'1')).status == palette::CalcStatus::none, L"bounded input");
    return failures ? 1 : 0;
}
