#include "core.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <limits>
#include <tuple>

namespace palette {

std::wstring fold(std::wstring_view text) {
    if (text.empty()) return {};
    const int input_size = static_cast<int>(std::min<std::size_t>(text.size(), INT_MAX));
    const int required = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE,
                                       text.data(), input_size, nullptr, 0, nullptr, nullptr, 0);
    if (required <= 0) return std::wstring(text);
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    const int written = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE,
                                      text.data(), input_size, result.data(), required,
                                      nullptr, nullptr, 0);
    if (written <= 0) return std::wstring(text);
    result.resize(static_cast<std::size_t>(written));
    return result;
}

namespace {

std::wstring_view trim(std::wstring_view text) {
    while (!text.empty() && iswspace(text.front())) text.remove_prefix(1);
    while (!text.empty() && iswspace(text.back())) text.remove_suffix(1);
    return text;
}

int match_score(std::wstring_view name, std::wstring_view query) {
    if (name == query) return 0;
    if (name.starts_with(query)) return 100;

    std::size_t position = name.find(query);
    if (position != std::wstring_view::npos) {
        const auto first=position;
        do {
            if(position==0 || !iswalnum(name[position-1])) return 200+static_cast<int>(std::min<std::size_t>(position,99));
            position=name.find(query,position+1);
        } while(position!=std::wstring_view::npos);
        return 300+static_cast<int>(std::min<std::size_t>(first,99));
    }

    std::size_t cursor = 0;
    std::size_t first = std::wstring_view::npos;
    std::size_t last = 0;
    for (const wchar_t wanted : query) {
        position = name.find(wanted, cursor);
        if (position == std::wstring_view::npos) return -1;
        if (first == std::wstring_view::npos) first = position;
        last = position;
        cursor = position + 1;
    }
    const std::size_t gaps = last - first + 1 - query.size();
    return 400 + static_cast<int>(std::min<std::size_t>(gaps + first, 999));
}

} // namespace

std::vector<SearchHit> search(const std::vector<AppEntry>& apps, std::wstring_view query, std::size_t limit) {
    if (limit == 0) return {};
    const std::wstring folded_query_storage = fold(trim(query));
    const std::wstring_view folded_query = folded_query_storage;

    std::vector<SearchHit> hits;
    hits.reserve(std::min(apps.size(), limit));
    for (std::size_t index = 0; index < apps.size(); ++index) {
        const auto& entry = apps[index];
        int score = 0;
        if (!folded_query.empty()) {
            const std::wstring generated = entry.folded.empty() ? fold(entry.name) : std::wstring{};
            const std::wstring_view name = entry.folded.empty() ? std::wstring_view(generated)
                                                                  : std::wstring_view(entry.folded);
            score = match_score(name, folded_query);
            if (score < 0) continue;
        }
        hits.push_back({index, score});
    }

    std::sort(hits.begin(), hits.end(), [&](const SearchHit& left, const SearchHit& right) {
        const auto& left_app = apps[left.index];
        const auto& right_app = apps[right.index];
        return std::tuple(left.score, std::numeric_limits<std::uint32_t>::max() - left_app.use_count,
                          std::numeric_limits<std::uint64_t>::max() - left_app.last_used, left.index) <
               std::tuple(right.score, std::numeric_limits<std::uint32_t>::max() - right_app.use_count,
                          std::numeric_limits<std::uint64_t>::max() - right_app.last_used, right.index);
    });
    if (hits.size() > limit) hits.resize(limit);
    return hits;
}

} // namespace palette
