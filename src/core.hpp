#pragma once
#include "model.hpp"
#include <string_view>
namespace palette {
CalcResult calculate(std::wstring_view expression);
std::wstring fold(std::wstring_view text);
std::vector<SearchHit> search(const std::vector<AppEntry>& apps, std::wstring_view query, std::size_t limit = 7);
}
