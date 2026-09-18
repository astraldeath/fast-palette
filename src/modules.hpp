#pragma once
#include "settings.hpp"
#include <optional>
namespace palette {
struct RoutedQuery {
    std::wstring text;
    std::optional<Module> exclusive;
    std::array<bool,module_count> enabled{};
    bool allows(Module module) const { return enabled[static_cast<size_t>(module)]; }
};
RoutedQuery route_query(std::wstring_view query,const Settings& settings);
std::vector<AppEntry> alias_entries(const Settings& settings);
}
