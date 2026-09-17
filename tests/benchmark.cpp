#include "core.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Percentiles {
    double p50;
    double p95;
};

Percentiles summarize(std::vector<double> samples) {
    std::sort(samples.begin(), samples.end());
    const auto at = [&](double percentile) {
        const std::size_t index = static_cast<std::size_t>(percentile * static_cast<double>(samples.size() - 1));
        return samples[index];
    };
    return {at(0.50), at(0.95)};
}

std::vector<palette::AppEntry> catalog() {
    std::vector<palette::AppEntry> apps;
    apps.reserve(5000);
    for (std::size_t index = 0; index < 5000; ++index) {
        palette::AppEntry entry;
        entry.id = L"benchmark-" + std::to_wstring(index);
        entry.name = L"Application Suite " + std::to_wstring(index) + L" Utility";
        if (index % 97 == 0) entry.name += L" Visual Studio Tools";
        entry.folded = palette::fold(entry.name);
        entry.use_count = static_cast<std::uint32_t>(index % 31);
        entry.last_used = index * 17;
        apps.push_back(std::move(entry));
    }
    return apps;
}

} // namespace

int main() {
    const auto apps = catalog();
    std::uint64_t checksum = 0;

    std::vector<double> search_samples;
    search_samples.reserve(1000);
    for (int iteration = 0; iteration < 1000; ++iteration) {
        const std::wstring_view query = iteration % 2 == 0 ? L"visual tools" : L"apsu4999";
        const auto start = Clock::now();
        const auto hits = palette::search(apps, query, 7);
        const auto end = Clock::now();
        search_samples.push_back(std::chrono::duration<double, std::micro>(end - start).count());
        checksum += hits.size();
        if (!hits.empty()) checksum += hits.front().index;
    }

    const std::wstring_view expressions[] = {
        L"2+3*4", L"sqrt(81)+ln(e)", L"2^3^2", L"sin(pi/4)^2+cos(pi/4)^2"
    };
    std::vector<double> calculation_samples;
    calculation_samples.reserve(10000);
    for (int iteration = 0; iteration < 10000; ++iteration) {
        const auto start = Clock::now();
        const auto result = palette::calculate(expressions[iteration % 4]);
        const auto end = Clock::now();
        calculation_samples.push_back(std::chrono::duration<double, std::micro>(end - start).count());
        checksum += result.status == palette::CalcStatus::value;
        checksum += static_cast<std::uint64_t>(result.text.size());
    }

    const auto search = summarize(std::move(search_samples));
    const auto calculation = summarize(std::move(calculation_samples));
    std::cout << std::fixed << std::setprecision(3)
              << "search 5000 apps (microseconds): p50=" << search.p50 << " p95=" << search.p95 << '\n'
              << "calculation (microseconds): p50=" << calculation.p50 << " p95=" << calculation.p95 << '\n'
              << "checksum=" << checksum << '\n';
    return 0;
}
