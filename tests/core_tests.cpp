#include "core.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char* description) {
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

void expect_value(std::wstring_view expression, double value, std::wstring_view text) {
    const auto result = palette::calculate(expression);
    expect(result.status == palette::CalcStatus::value, "expression produces a value");
    expect(result.value == value, "expression has expected numeric value");
    expect(result.text == text, "expression has expected formatted text");
}

palette::AppEntry app(std::wstring name, std::uint32_t use_count = 0, std::uint64_t last_used = 0) {
    palette::AppEntry result;
    result.name = std::move(name);
    result.folded = palette::fold(result.name);
    result.use_count = use_count;
    result.last_used = last_used;
    return result;
}

void calculator_examples() {
    expect_value(L"2+3*4", 14.0, L"14");
    expect_value(L"-2^2", -4.0, L"-4");
    expect_value(L"2^3^2", 512.0, L"512");
    expect_value(L"2^-2", 0.25, L".25");
    expect_value(L"100+10%", 100.1, L"100.1");
    expect_value(L"sqrt(81)", 9.0, L"9");
    expect_value(L"ln(e)", 1.0, L"1");
    expect_value(L"log10(100)", 2.0, L"2");
    expect_value(L"0!", 1.0, L"1");
    expect_value(L"5!", 120.0, L"120");
    expect_value(L"(5+3)!", 40320.0, L"40320");
    expect_value(L"2*3!+1", 13.0, L"13");
    expect_value(L"2^3!", 64.0, L"64");
    expect_value(L"-3!", -6.0, L"-6");
    expect_value(L"= 5 ! / 4!", 5.0, L"5");
    const auto large = palette::calculate(L"100!");
    expect(large.status == palette::CalcStatus::value &&
           std::abs(large.value / 9.332621544394415e157 - 1.0) < 1e-14,
           "100 factorial produces a finite accurate result");
    expect(palette::calculate(L"170!").status == palette::CalcStatus::value,
           "largest representable factorial succeeds");
    for (auto input : {L"(-1)!", L"2.5!", L"171!", L"1e100!", L"5!!"}) {
        expect(palette::calculate(input).status == palette::CalcStatus::error,
               "invalid, overflowing, or ambiguous factorial is rejected");
    }
}

void calculator_classification_and_bounds() {
    expect(palette::calculate(L"1/0").status == palette::CalcStatus::error,
           "division by zero is an error");
    expect(palette::calculate(L"sqrt(-1)").status == palette::CalcStatus::error,
           "negative square root is an error");
    expect(palette::calculate(L"2+").status == palette::CalcStatus::incomplete,
           "trailing operator is incomplete");
    expect(palette::calculate(L"Visual Studio").status == palette::CalcStatus::none,
           "ordinary app name is not treated as a calculation");
    expect(palette::calculate(L"=wat").status == palette::CalcStatus::error,
           "equals prefix forces calculator errors");
    expect(palette::calculate(std::wstring(513, L'1')).status == palette::CalcStatus::error,
           "overlong expression is bounded");

    std::wstring nesting(65, L'(');
    nesting += L"1";
    nesting.append(65, L')');
    expect(palette::calculate(nesting).status == palette::CalcStatus::error,
           "excessive nesting is bounded");
}

void calculator_extended_math() {
    expect_value(L"2(3+4)", 14.0, L"14");
    expect_value(L"(2+3)(4+5)", 45.0, L"45");
    expect_value(L"2sin(pi/2)", 2.0, L"2");
    expect_value(L"6/2(1+2)", 9.0, L"9");
    expect_value(L"2(3!)^2", 72.0, L"72");
    expect_value(L"2^3(4)", 32.0, L"32");
    expect_value(L"2e3", 2000.0, L"2000");
    expect_value(L"2e-3", 0.002, L".002");
    expect_value(L"log(100)", 2.0, L"2");
    expect_value(L"round(-2.5)", -3.0, L"-3");
    expect_value(L"floor(-2.1)", -3.0, L"-3");
    expect_value(L"ceil(-2.1)", -2.0, L"-2");
    expect_value(L"trunc(-2.9)", -2.0, L"-2");
    const auto pi = palette::calculate(L"pi");
    const auto implicit = palette::calculate(L"2pi");
    expect(implicit.status == palette::CalcStatus::value && implicit.value == 2 * pi.value,
           "number next to pi implicitly multiplies");
    for (auto input : {L"Log Viewer", L"Round Table", L"Asin Tools", L"Sinatra"})
        expect(palette::calculate(input).status == palette::CalcStatus::none,
               "function-like app names remain searchable");
    for (auto input : {L"log(0)", L"asin(2)", L"acos(-2)", L"2 3", L"2.3.4"})
        expect(palette::calculate(input).status == palette::CalcStatus::error,
               "invalid extended math is rejected");
    for (auto input : {L"2(", L"2sin(", L"log(", L"2e-"})
        expect(palette::calculate(input).status == palette::CalcStatus::incomplete,
               "unfinished extended math remains incomplete");
}

void calculator_angle_units() {
    const auto check = [](std::wstring_view expression, double expected, bool degrees) {
        const auto result = palette::calculate(expression, degrees);
        expect(result.status == palette::CalcStatus::value && std::abs(result.value - expected) < 1e-12,
               "trigonometry respects the selected angle unit");
    };
    check(L"sin(30)", 0.5, true);
    check(L"cos(60)", 0.5, true);
    check(L"tan(45)", 1.0, true);
    check(L"asin(.5)", 30.0, true);
    check(L"acos(.5)", 60.0, true);
    check(L"atan(1)", 45.0, true);
    check(L"sin(pi/2)", 1.0, false);
    check(L"asin(1)", 1.5707963267948966, false);
    check(L"acos(-1)", 3.141592653589793, false);
    check(L"atan(1)", 0.7853981633974483, false);
    check(L"2sin(30)+round(2.5)", 4.0, true);
    check(L"sin(asin(.5))", 0.5, true);
    check(L"ln(e)", 1.0, true);
    for (bool degrees : {false, true}) {
        expect(palette::calculate(L"asin(2)", degrees).status == palette::CalcStatus::error,
               "inverse trig domain errors remain errors in both angle modes");
    }
}

void calculator_round_trip_formatting() {
    const std::vector<std::wstring> inputs = {
        L"1/3", L"1e20+1", L"-0.000000123456789", L"pi", L"sin(.5)", L"100!"
    };
    for (const auto& input : inputs) {
        const auto result = palette::calculate(input);
        expect(result.status == palette::CalcStatus::value, "round-trip input calculates");
        if (result.status == palette::CalcStatus::value) {
            wchar_t* end = nullptr;
            const double parsed = std::wcstod(result.text.c_str(), &end);
            expect(end && *end == L'\0', "formatted result is parseable");
            expect(parsed == result.value, "formatted result round-trips exactly");
        }
    }
}

void search_ranking() {
    const std::vector<palette::AppEntry> apps = {
        app(L"Notepad++", 100, 100),
        app(L"Notepad"),
        app(L"My Visual Studio Helper", 100, 100),
        app(L"Visual Studio")
    };
    const auto notepad = palette::search(apps, L"NOTEPAD");
    expect(notepad.size() >= 2, "exact search finds both Notepad entries");
    if (notepad.size() >= 2) {
        expect(notepad[0].index == 1, "exact match beats a highly-used prefix match");
        expect(notepad[1].index == 0, "prefix match remains visible");
    }
    const auto visual = palette::search(apps, L"visual studio");
    expect(visual.size() >= 2, "prefix and substring matches are found");
    if (visual.size() >= 2) {
        expect(visual[0].index == 3, "prefix match beats highly-used substring match");
        expect(visual[1].index == 2, "substring match follows prefix match");
    }
}

void search_matching_and_limits() {
    const auto later_boundary=palette::search({app(L"Xpad"),app(L"Notepad Pad")},L"pad");
    expect(later_boundary.size()==2 && later_boundary[0].index==1,"later word-start match outranks an earlier substring");
    const std::vector<palette::AppEntry> apps = {
        app(L"\u00C9diteur"), app(L"Calculator"), app(L"Calendar"), app(L"Camera")
    };
    const auto unicode = palette::search(apps, L"\u00E9diteur");
    expect(unicode.size() == 1 && unicode[0].index == 0,
           "Unicode case-insensitive exact matching works");
    expect(palette::search(apps, L"qzx").empty(), "missing subsequence returns no hits");
    expect(palette::search(apps, L"ca", 2).size() == 2, "result count obeys the limit");
    expect(palette::search(apps, L"ca", 0).empty(), "zero limit returns no hits");
}

void search_empty_query_usage_order() {
    const std::vector<palette::AppEntry> apps = {
        app(L"Recent", 5, 300),
        app(L"Frequent", 10, 100),
        app(L"Older tie", 5, 200)
    };
    const auto hits = palette::search(apps, L"");
    expect(hits.size() == 3, "empty query returns catalog entries");
    if (hits.size() == 3) {
        expect(hits[0].index == 1, "empty query prioritizes use count");
        expect(hits[1].index == 0, "empty query uses recency to break use-count ties");
        expect(hits[2].index == 2, "older equal-use entry follows newer entry");
    }
}

} // namespace

int main() {
    calculator_examples();
    calculator_extended_math();
    calculator_angle_units();
    calculator_classification_and_bounds();
    calculator_round_trip_formatting();
    search_ranking();
    search_matching_and_limits();
    search_empty_query_usage_order();
    if (failures != 0) {
        std::cerr << failures << " core test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "core tests passed\n";
    return EXIT_SUCCESS;
}
