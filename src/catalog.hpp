#pragma once
#include "model.hpp"
#include <windows.h>
#include <stop_token>
#include <string_view>
namespace palette {
namespace detail {
std::wstring installed_app_executable(std::wstring_view name,std::wstring_view location,std::wstring_view icon);
bool path_is_within_directory(std::wstring_view path, std::wstring_view directory);
}
std::vector<AppEntry> discover_apps(std::stop_token stop = {});
enum class ResultAction { open, run_as_admin, open_folder };
std::wstring resolved_app_path(const AppEntry& app);
bool supports_action(const AppEntry& app, ResultAction action);
bool launch_app(const AppEntry& app, std::wstring& error, ResultAction action = ResultAction::open);
HICON load_app_icon(const AppEntry& app); // caller owns returned icon
std::wstring shortcut_identity(const std::wstring& path, std::wstring* command = nullptr);
}
