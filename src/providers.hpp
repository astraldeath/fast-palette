#pragma once
#include "model.hpp"
#include "settings.hpp"
#include <windows.h>
#include <functional>
#include <span>
#include <cstddef>
namespace palette {
struct EverythingQuery { bool enabled=false,exclusive=false; std::wstring text; };
EverythingQuery route_everything(std::wstring_view query,const Settings& settings);
const std::vector<AppEntry>& windows_settings();
std::wstring expand_path_query(std::wstring_view query);
std::vector<AppEntry> path_results(std::wstring_view query);
std::vector<AppEntry> parse_everything_reply(std::span<const std::byte> bytes);
struct EverythingReply { bool available=false; std::vector<AppEntry> entries; };
HWND everything_window();
EverythingReply query_everything(HWND server,std::wstring_view query,const std::function<bool()>& cancelled);
}
