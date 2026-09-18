#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace palette {
enum class LaunchKind { shortcut, shell, executable };
enum class SearchSource { application, windows_settings, path, file, alias };
struct AppEntry {
    std::wstring id, name, detail, target;
    LaunchKind kind = LaunchKind::shortcut;
    std::wstring folded;
    std::uint32_t use_count = 0;
    std::uint64_t last_used = 0;
    SearchSource source = SearchSource::application;
    bool is_folder = false;
    std::wstring arguments;
};
struct SearchHit { std::size_t index; int score; };
enum class CalcStatus { none, incomplete, value, error };
struct CalcResult {
    CalcStatus status = CalcStatus::none;
    double value = 0;
    std::wstring text;
};
}
