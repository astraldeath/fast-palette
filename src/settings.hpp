#pragma once
#include "model.hpp"
#include <windows.h>
#include <string>
#include <vector>
namespace palette {
inline constexpr wchar_t settings_key[] = L"Software\\FastPalette";
struct Hotkey {
    UINT modifiers=0, key=0;
    bool operator==(const Hotkey&) const = default;
};
struct Settings {
    bool left_win = true;
    bool right_win = true;
    bool start_at_login = false;
    UINT modifiers = MOD_CONTROL | MOD_ALT;
    UINT key = VK_SPACE;
    std::vector<Hotkey> extra_bindings;
    std::vector<std::wstring> portable_apps;
};
Settings load_settings(const wchar_t* key = settings_key);
bool save_settings(const Settings& settings, std::wstring& error, const wchar_t* key = settings_key);
bool set_start_at_login(bool enabled, std::wstring& error);
void load_usage(std::vector<AppEntry>& apps);
void save_usage(const AppEntry& app);
bool valid_hotkey(UINT modifiers, UINT key);
std::wstring hotkey_label(const Settings& settings);
std::wstring hotkey_label(const Hotkey& hotkey);
std::vector<Hotkey> all_hotkeys(const Settings& settings);
bool show_settings_dialog(HWND owner, Settings& settings);
}
