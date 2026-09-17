#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace palette {
enum class LaunchKind { shortcut, shell, executable };
struct AppEntry {
    std::wstring id, name, detail, target;
    LaunchKind kind = LaunchKind::shortcut;
    std::wstring folded;
    std::uint32_t use_count = 0;
    std::uint64_t last_used = 0;
};
struct SearchHit { std::size_t index; int score; };
enum class CalcStatus { none, incomplete, value, error };
struct CalcResult {
    CalcStatus status = CalcStatus::none;
    double value = 0;
    std::wstring text;
};
}
