#include "conversions.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <vector>

namespace palette {
namespace {
enum class Dimension { length, mass, temperature, time, storage, rate };
struct Unit { std::wstring alias, label; Dimension dimension; double scale, offset = 0; bool mixed = false; };
std::wstring lower(std::wstring_view s) {
    std::wstring r(s);
    for (auto& c : r) if (c >= L'A' && c <= L'Z') c += L'a' - L'A';
    return r;
}
std::wstring_view trim(std::wstring_view s) {
    while (!s.empty() && (s.front() == L' ' || s.front() == L'\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == L' ' || s.back() == L'\t')) s.remove_suffix(1);
    return s;
}
const std::vector<Unit>& units() {
    static const auto table = [] {
        std::vector<Unit> v;
        auto add = [&](std::wstring_view aliases, const wchar_t* label, Dimension d, double scale, double offset = 0, bool mixed = false) {
            while (!aliases.empty()) {
                auto end = aliases.find(L'|');
                v.push_back({std::wstring(aliases.substr(0, end)), label, d, scale, offset, mixed});
                if (end == aliases.npos) break;
                aliases.remove_prefix(end + 1);
            }
        };
        add(L"mm|millimeter|millimeters|millimetre|millimetres", L"mm", Dimension::length, .001);
        add(L"cm|centimeter|centimeters|centimetre|centimetres", L"cm", Dimension::length, .01);
        add(L"m|meter|meters|metre|metres", L"m", Dimension::length, 1);
        add(L"km|kilometer|kilometers|kilometre|kilometres", L"km", Dimension::length, 1000);
        add(L"in|inch|inches|\"", L"in", Dimension::length, .0254);
        add(L"ft|foot|feet|'", L"ft", Dimension::length, .3048);
        add(L"yd|yard|yards", L"yd", Dimension::length, .9144);
        add(L"mi|mile|miles", L"mi", Dimension::length, 1609.344);
        add(L"ftin|feet and inches|ft+in", L"ftin", Dimension::length, .0254, 0, true);
        add(L"g|gram|grams", L"g", Dimension::mass, .001);
        add(L"kg|kilogram|kilograms", L"kg", Dimension::mass, 1);
        add(L"lb|lbs|pound|pounds", L"lb", Dimension::mass, .45359237);
        add(L"oz|ounce|ounces", L"oz", Dimension::mass, .028349523125);
        add(L"c|celsius|°c", L"C", Dimension::temperature, 1, 273.15);
        add(L"f|fahrenheit|°f", L"F", Dimension::temperature, 5.0 / 9, 255.37222222222222);
        add(L"k|kelvin", L"K", Dimension::temperature, 1);
        add(L"ms|millisecond|milliseconds", L"ms", Dimension::time, .001);
        add(L"s|sec|second|seconds", L"s", Dimension::time, 1);
        add(L"min|minute|minutes", L"min", Dimension::time, 60);
        add(L"h|hr|hour|hours", L"h", Dimension::time, 3600);
        add(L"d|day|days", L"d", Dimension::time, 86400);
        add(L"b|byte|bytes", L"B", Dimension::storage, 1);
        add(L"kb|kilobyte|kilobytes", L"KB", Dimension::storage, 1e3);
        add(L"mb|megabyte|megabytes", L"MB", Dimension::storage, 1e6);
        add(L"gb|gigabyte|gigabytes", L"GB", Dimension::storage, 1e9);
        add(L"tb|terabyte|terabytes", L"TB", Dimension::storage, 1e12);
        add(L"kib", L"KiB", Dimension::storage, 1024);
        add(L"mib", L"MiB", Dimension::storage, 1048576);
        add(L"gib", L"GiB", Dimension::storage, 1073741824);
        add(L"tib", L"TiB", Dimension::storage, 1099511627776.0);
        for (auto [prefix, scale] : {std::pair{L"", 1.0}, {L"k", 1e3}, {L"m", 1e6}, {L"g", 1e9}, {L"t", 1e12}}) {
            std::wstring p(prefix), display = p;
            if (!display.empty()) display[0] -= L'a' - L'A';
            add(p + L"bps|" + p + L"b/s", (display + L"bps").c_str(), Dimension::rate, scale);
        }
        return v;
    }();
    return table;
}
bool lookup(std::wstring_view token, Unit& out) {
    auto folded = lower(trim(token));
    for (const auto& u : units()) if (folded == u.alias) {
        out = u;
        if (u.dimension == Dimension::rate && token.find(L'B') != token.npos) {
            out.scale *= 8;
            out.label = out.label.substr(0, out.label.size() - 3) + L"B/s";
        }
        return true;
    }
    return false;
}
bool number(std::wstring_view& s, double& value) {
    s = trim(s);
    std::size_t end = 0;
    if (end < s.size() && (s[end] == L'+' || s[end] == L'-')) ++end;
    while (end < s.size() && ((s[end] >= L'0' && s[end] <= L'9') || s[end] == L'.')) ++end;
    if (end < s.size() && (s[end] == L'e' || s[end] == L'E')) {
        ++end;
        if (end < s.size() && (s[end] == L'+' || s[end] == L'-')) ++end;
        while (end < s.size() && s[end] >= L'0' && s[end] <= L'9') ++end;
    }
    if (!end) return false;
    std::string ascii;
    for (auto c : s.substr(0, end)) ascii.push_back(static_cast<char>(c));
    auto begin = ascii.data();
    if (*begin == '+') ++begin;
    auto result = std::from_chars(begin, ascii.data() + ascii.size(), value);
    s.remove_prefix(end);
    if (result.ec != std::errc{} || result.ptr != ascii.data() + ascii.size()) { value = NAN; return true; }
    return true;
}
bool consume_unit(std::wstring_view& s, Unit& unit) {
    s = trim(s);
    std::size_t best = 0;
    for (const auto& u : units()) {
        const auto n = u.alias.size();
        if (n <= best || n > s.size() || lower(s.substr(0, n)) != u.alias) continue;
        if (n < s.size() && ((s[n] >= L'a' && s[n] <= L'z') || (s[n] >= L'A' && s[n] <= L'Z') || s[n] == L'/')) continue;
        best = n;
    }
    if (!best || !lookup(s.substr(0, best), unit)) return false;
    s = trim(s.substr(best));
    return true;
}
std::wstring format(double n) {
    if (n == 0) n = 0; // Suppress negative zero without discarding tiny conversions.
    std::wostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(12) << n;
    return stream.str();
}
CalcResult fail(CalcStatus status, const wchar_t* message) { return {status, 0, message}; }
}

CalcResult convert_units(std::wstring_view query) {
    if (query.size() > 512) return {};
    auto remaining = trim(query);
    double input = 0;
    if (!number(remaining, input)) return {};
    Unit source, target;
    if (!consume_unit(remaining, source)) return {};
    if (source.mixed) return fail(CalcStatus::error, L"Use feet and inches, for example 5 ft 11 in");
    if (!std::isfinite(input)) return fail(CalcStatus::error, L"Invalid number");
    double base = input * source.scale + source.offset;
    if (source.label == L"ft" && !remaining.empty() && (remaining.front() == L'.' || (remaining.front() >= L'0' && remaining.front() <= L'9'))) {
        double inches = 0;
        Unit inch_unit;
        if (!number(remaining, inches) || !consume_unit(remaining, inch_unit) || inch_unit.label != L"in" || !std::isfinite(inches))
            return fail(CalcStatus::error, L"Expected inches after feet");
        base += (std::signbit(input) ? -1 : 1) * inches * inch_unit.scale;
    }
    if (remaining.empty()) {
        std::wstring destination;
        switch (source.dimension) {
        case Dimension::length: destination = (source.label == L"ft" || source.label == L"in" || source.mixed) ? L"cm" : L"ftin"; break;
        case Dimension::mass: destination = source.label == L"kg" ? L"lb" : L"kg"; break;
        case Dimension::temperature: destination = source.label == L"C" ? L"F" : L"C"; break;
        case Dimension::time: destination = source.label == L"s" ? L"min" : L"s"; break;
        case Dimension::storage: destination = source.label == L"GB" ? L"MB" : source.label == L"GiB" ? L"MiB" : L"GB"; break;
        case Dimension::rate: destination = source.label.find(L'B') == source.label.npos ? L"MB/s" : L"Mbps"; break;
        }
        lookup(destination, target);
    } else {
        bool separator = false;
        for (auto token : {L"->", L"to", L"in", L"as"}) {
            const std::wstring_view t(token);
            if (lower(remaining.substr(0, t.size())) == t && (t == L"->" || remaining.size() == t.size() || remaining[t.size()] == L' ' || remaining[t.size()] == L'\t')) {
                remaining = trim(remaining.substr(t.size())); separator = true; break;
            }
        }
        if (!separator) {
            for (auto token : {L"to", L"in", L"as", L"->"})
                if (std::wstring_view(token).starts_with(lower(remaining)))
                    return fail(CalcStatus::incomplete, L"Finish the conversion");
            return {};
        }
        if (remaining.empty()) return fail(CalcStatus::incomplete, L"Choose a destination unit");
        if (!lookup(remaining, target)) {
            for (const auto& u : units()) if (u.alias.starts_with(lower(remaining))) return fail(CalcStatus::incomplete, L"Finish the destination unit");
            return fail(CalcStatus::error, L"Unknown destination unit");
        }
    }
    if (source.dimension != target.dimension) return fail(CalcStatus::error, L"Incompatible units");
    double result = (base - target.offset) / target.scale;
    if (source.dimension == Dimension::temperature && std::abs(result) < 1e-12) result = 0;
    if (!std::isfinite(base) || !std::isfinite(result)) return fail(CalcStatus::error, L"Conversion is out of range");
    if (target.mixed) {
        const double rounded = std::round(std::abs(result) * 100) / 100;
        if (!std::isfinite(rounded)) return fail(CalcStatus::error, L"Conversion is out of range");
        const double feet = std::floor(rounded / 12);
        const double inches = std::round((rounded - feet * 12) * 100) / 100;
        return {CalcStatus::value, result, (result < 0 && rounded != 0 ? L"-" : L"") + format(feet) + L" ft " + format(inches) + L" in"};
    }
    return {CalcStatus::value, result, format(result) + L" " + target.label};
}
}
