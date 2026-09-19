#pragma once
#include "model.hpp"
#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include <array>
namespace palette {
inline constexpr wchar_t settings_key[] = L"Software\\FastPalette";
struct Hotkey {
    UINT modifiers=0, key=0;
    bool operator==(const Hotkey&) const = default;
};
enum class Module : size_t { apps, calculator, settings, paths, everything, conversions, aliases, count };
inline constexpr size_t module_count=static_cast<size_t>(Module::count);
inline constexpr std::array<const wchar_t*,module_count> module_keys={L"Apps",L"Calculator",L"Settings",L"Paths",L"Everything",L"Conversions",L"Aliases"};
struct PrefixRule {
    std::wstring prefix;
    bool only=false;
    bool operator==(const PrefixRule&) const = default;
};
struct Alias {
    std::wstring name,target,arguments;
    bool operator==(const Alias&) const = default;
};
struct Settings {
    bool left_win = true;
    bool right_win = true;
    bool start_at_login = false;
    bool automatic_updates = true;
    bool search_apps = true;
    bool search_calculator = true;
    bool search_settings = true;
    bool search_paths = true;
    bool search_everything = false;
    bool everything_prefix_only = false;
    std::wstring everything_prefix = L"?";
    bool search_conversions = true;
    bool search_aliases = true;
    bool calculator_degrees = false;
    std::array<PrefixRule,module_count> prefixes={PrefixRule{L"app"},PrefixRule{L"="},PrefixRule{L"win"},PrefixRule{L"path"},PrefixRule{L"?"},PrefixRule{L"conv"},PrefixRule{L"@"}};
    std::vector<Alias> aliases;
    UINT modifiers = MOD_CONTROL | MOD_ALT;
    UINT key = VK_SPACE;
    std::vector<Hotkey> extra_bindings;
    std::vector<std::wstring> portable_apps;
};
Settings load_settings(const wchar_t* key = settings_key);
bool valid_everything_prefix(std::wstring_view prefix);
PrefixRule module_rule(const Settings& settings,Module module);
void set_module_rule(Settings& settings,Module module,const PrefixRule& rule);
bool module_enabled(const Settings& settings,Module module);
bool valid_module_settings(const Settings& settings,std::wstring& error);
bool save_settings(const Settings& settings, std::wstring& error, const wchar_t* key = settings_key);
bool set_start_at_login(bool enabled, std::wstring& error,
    const wchar_t* run_key=L"Software\\Microsoft\\Windows\\CurrentVersion\\Run");
void load_usage(std::vector<AppEntry>& apps);
void save_usage(const AppEntry& app);
bool valid_hotkey(UINT modifiers, UINT key);
std::wstring hotkey_label(const Settings& settings);
std::wstring hotkey_label(const Hotkey& hotkey);
std::vector<Hotkey> all_hotkeys(const Settings& settings);
using UpdateCheck = std::function<void(std::function<void()>)>;
bool show_settings_dialog(HWND owner, Settings& settings,const UpdateCheck& check_updates={});
}
