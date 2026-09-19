#include "settings.hpp"
#include "win_util.hpp"
#include <algorithm>
#include <array>
#include <cwctype>

namespace palette {
namespace {
DWORD read_dword(HKEY key, const wchar_t* name, DWORD fallback) {
    DWORD value=0, size=sizeof(value);
    return RegGetValueW(key,nullptr,name,RRF_RT_REG_DWORD,nullptr,&value,&size)==ERROR_SUCCESS ? value : fallback;
}
std::wstring usage_name(const std::wstring& id) {
    constexpr wchar_t digits[]=L"0123456789abcdef";
    std::wstring out; out.reserve(id.size()*4);
    for (wchar_t c:id) for (int shift=12;shift>=0;shift-=4) out.push_back(digits[(c>>shift)&15]);
    return out;
}
struct Usage { std::uint64_t count, last; };
}
bool valid_hotkey(UINT mods, UINT key) {
    if (!(mods & (MOD_CONTROL|MOD_ALT|MOD_SHIFT|MOD_WIN)) || mods & ~(MOD_CONTROL|MOD_ALT|MOD_SHIFT|MOD_WIN)) return false;
    if((mods&MOD_WIN) && key=='L')return false;
    if (key<0x08 || key>0xFE || key==VK_F12 || key==VK_LWIN || key==VK_RWIN) return false;
    return key!=VK_SHIFT && key!=VK_CONTROL && key!=VK_MENU && key!=VK_LSHIFT && key!=VK_RSHIFT &&
        key!=VK_LCONTROL && key!=VK_RCONTROL && key!=VK_LMENU && key!=VK_RMENU;
}
bool valid_everything_prefix(std::wstring_view prefix) {
    return !prefix.empty() && prefix.size()<=16 && prefix.front()!=L'=' &&
        std::none_of(prefix.begin(),prefix.end(),[](wchar_t c){return iswspace(c) || iswcntrl(c);});
}
Settings load_settings(const wchar_t* path) {
    Settings s; RegistryKey key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,path,0,KEY_QUERY_VALUE,&key.value)!=ERROR_SUCCESS) return s;
    s.left_win=read_dword(key.value,L"LeftWin",1)!=0;
    s.right_win=read_dword(key.value,L"RightWin",1)!=0;
    s.start_at_login=read_dword(key.value,L"StartAtLogin",0)!=0;
    s.automatic_updates=read_dword(key.value,L"AutomaticUpdates",1)!=0;
    s.search_apps=read_dword(key.value,L"SearchApps",1)!=0;
    s.search_calculator=read_dword(key.value,L"SearchCalculator",1)!=0;
    s.search_settings=read_dword(key.value,L"SearchSettings",1)!=0;
    s.search_paths=read_dword(key.value,L"SearchPaths",1)!=0;
    s.search_everything=read_dword(key.value,L"SearchEverything",0)!=0;
    s.search_conversions=read_dword(key.value,L"SearchConversions",1)!=0;
    s.search_aliases=read_dword(key.value,L"SearchAliases",1)!=0;
    s.search_currency=read_dword(key.value,L"SearchCurrency",1)!=0;
    s.calculator_degrees=read_dword(key.value,L"CalculatorDegrees",0)!=0;
    s.everything_prefix_only=read_dword(key.value,L"EverythingPrefixOnly",0)!=0;
    std::array<wchar_t,18> prefix{};DWORD prefix_bytes=sizeof(prefix);
    if(RegGetValueW(key.value,nullptr,L"EverythingPrefix",RRF_RT_REG_SZ,nullptr,prefix.data(),&prefix_bytes)==ERROR_SUCCESS && (!prefix[0] || valid_everything_prefix(prefix.data())))
        s.everything_prefix=prefix.data();
    std::array<bool,module_count> stored_prefix{};
    for(size_t i=0;i<module_count;++i){if(i==static_cast<size_t>(Module::everything))continue;
        const auto name=std::wstring(module_keys[i])+L"Prefix";prefix.fill(0);prefix_bytes=sizeof(prefix);
        if(RegGetValueW(key.value,nullptr,name.c_str(),RRF_RT_REG_SZ,nullptr,prefix.data(),&prefix_bytes)==ERROR_SUCCESS){s.prefixes[i].prefix=prefix.data();stored_prefix[i]=true;}
        s.prefixes[i].only=read_dword(key.value,(name+L"Only").c_str(),0)!=0;
    }
    // New module defaults must yield to existing user-defined prefixes.
    const auto preserve_legacy_prefix=[&]{for(size_t i=0;i<module_count;++i)if(i!=static_cast<size_t>(Module::everything) && !stored_prefix[i]){
        bool conflict=true;while(conflict){conflict=false;for(size_t j=0;j<module_count;++j)if(i!=j &&
            _wcsicmp(s.prefixes[i].prefix.c_str(),module_rule(s,static_cast<Module>(j)).prefix.c_str())==0){s.prefixes[i].prefix+=L":";conflict=true;break;}}
    }};
    preserve_legacy_prefix();
    std::wstring validation_error;
    if(!valid_module_settings(s,validation_error)){s.prefixes=Settings{}.prefixes;stored_prefix.fill(false);if(s.everything_prefix.empty())s.everything_prefix_only=false;preserve_legacy_prefix();}
    const auto mods=read_dword(key.value,L"Modifiers",s.modifiers), vk=read_dword(key.value,L"Key",s.key);
    if (valid_hotkey(mods,vk)) { s.modifiers=mods; s.key=vk; }
    std::array<Hotkey,15> extra{}; DWORD extra_bytes=sizeof(extra);
    if(RegGetValueW(key.value,nullptr,L"ExtraBindings",RRF_RT_REG_BINARY,nullptr,extra.data(),&extra_bytes)==ERROR_SUCCESS && extra_bytes%sizeof(Hotkey)==0) {
        for(size_t i=0;i<extra_bytes/sizeof(Hotkey);++i) {
            const auto binding=extra[i];
            if(valid_hotkey(binding.modifiers,binding.key) && !(binding==Hotkey{s.modifiers,s.key}) &&
                std::find(s.extra_bindings.begin(),s.extra_bindings.end(),binding)==s.extra_bindings.end()) s.extra_bindings.push_back(binding);
        }
    }
    DWORD bytes=0;
    if (RegGetValueW(key.value,nullptr,L"PortableApps",RRF_RT_REG_MULTI_SZ,nullptr,nullptr,&bytes)==ERROR_SUCCESS && bytes<=1024*1024) {
        std::vector<wchar_t> data(bytes/sizeof(wchar_t)+2,0);
        if (RegGetValueW(key.value,nullptr,L"PortableApps",RRF_RT_REG_MULTI_SZ,nullptr,data.data(),&bytes)==ERROR_SUCCESS) {
            for (const wchar_t* p=data.data(); *p; p+=wcslen(p)+1) s.portable_apps.emplace_back(p);
        }
    }
    if(RegGetValueW(key.value,nullptr,L"Aliases",RRF_RT_REG_MULTI_SZ,nullptr,nullptr,&bytes)==ERROR_SUCCESS && bytes<=4*1024*1024){
        std::vector<wchar_t> data(bytes/sizeof(wchar_t)+2,0);
        if(RegGetValueW(key.value,nullptr,L"Aliases",RRF_RT_REG_MULTI_SZ,nullptr,data.data(),&bytes)==ERROR_SUCCESS){
            std::vector<std::wstring> fields;for(const wchar_t* p=data.data();*p && fields.size()<384;p+=wcslen(p)+1)fields.emplace_back(p);
            for(size_t i=0;i+2<fields.size();i+=3)if(fields[i+2].starts_with(L"="))s.aliases.push_back({fields[i],fields[i+1],fields[i+2].substr(1)});
            if(!valid_module_settings(s,validation_error))s.aliases.clear();
        }
    }
    return s;
}
bool save_settings(const Settings& s, std::wstring& error, const wchar_t* path) {
    if(!valid_module_settings(s,error))return false;
    if (!valid_hotkey(s.modifiers,s.key)) { error=L"Choose a key with Ctrl, Alt, Shift, or Win. Win+L and F12 are reserved."; return false; }
    const auto bindings=all_hotkeys(s);
    if(bindings.size()>16) {error=L"Up to 16 keyboard shortcuts are supported.";return false;}
    for(size_t i=0;i<bindings.size();++i) {
        if(!valid_hotkey(bindings[i].modifiers,bindings[i].key) || std::find(bindings.begin(),bindings.begin()+i,bindings[i])!=bindings.begin()+i) {
            error=L"Each keyboard shortcut must be valid and unique.";return false;
        }
    }
    RegistryKey key;
    auto status=RegCreateKeyExW(HKEY_CURRENT_USER,path,0,nullptr,0,KEY_SET_VALUE,nullptr,&key.value,nullptr);
    if (status!=ERROR_SUCCESS) { error=system_error(status); return false; }
    std::vector<wchar_t> paths;
    for (const auto& app:s.portable_apps) { paths.insert(paths.end(),app.begin(),app.end()); paths.push_back(0); }
    paths.push_back(0); if (paths.size()==1) paths.push_back(0);
    status=RegSetValueExW(key.value,L"PortableApps",0,REG_MULTI_SZ,reinterpret_cast<const BYTE*>(paths.data()),static_cast<DWORD>(paths.size()*sizeof(wchar_t)));
    if(status==ERROR_SUCCESS) status=RegSetValueExW(key.value,L"ExtraBindings",0,REG_BINARY,reinterpret_cast<const BYTE*>(s.extra_bindings.data()),static_cast<DWORD>(s.extra_bindings.size()*sizeof(Hotkey)));
    if(status==ERROR_SUCCESS)status=RegSetValueExW(key.value,L"EverythingPrefix",0,REG_SZ,reinterpret_cast<const BYTE*>(s.everything_prefix.c_str()),static_cast<DWORD>((s.everything_prefix.size()+1)*sizeof(wchar_t)));
    for(size_t i=0;i<module_count && status==ERROR_SUCCESS;++i){if(i==static_cast<size_t>(Module::everything))continue;
        const auto name=std::wstring(module_keys[i])+L"Prefix";const auto& rule=s.prefixes[i];const DWORD only=rule.only;
        status=RegSetValueExW(key.value,name.c_str(),0,REG_SZ,reinterpret_cast<const BYTE*>(rule.prefix.c_str()),static_cast<DWORD>((rule.prefix.size()+1)*sizeof(wchar_t)));
        if(status==ERROR_SUCCESS)status=RegSetValueExW(key.value,(name+L"Only").c_str(),0,REG_DWORD,reinterpret_cast<const BYTE*>(&only),sizeof(only));
    }
    std::vector<wchar_t> alias_data;
    for(const auto& alias:s.aliases)for(const auto& field:{alias.name,alias.target,L"="+alias.arguments}){alias_data.insert(alias_data.end(),field.begin(),field.end());alias_data.push_back(0);}
    alias_data.push_back(0);if(alias_data.size()==1)alias_data.push_back(0);
    if(status==ERROR_SUCCESS)status=RegSetValueExW(key.value,L"Aliases",0,REG_MULTI_SZ,reinterpret_cast<const BYTE*>(alias_data.data()),static_cast<DWORD>(alias_data.size()*sizeof(wchar_t)));
    const std::pair<const wchar_t*,DWORD> values[]={ {L"LeftWin",s.left_win},{L"RightWin",s.right_win},{L"StartAtLogin",s.start_at_login},{L"Modifiers",s.modifiers},{L"Key",s.key},
        {L"SearchApps",s.search_apps},{L"SearchCalculator",s.search_calculator},{L"SearchSettings",s.search_settings},
        {L"SearchPaths",s.search_paths},{L"SearchEverything",s.search_everything},{L"EverythingPrefixOnly",s.everything_prefix_only},{L"AutomaticUpdates",s.automatic_updates},
        {L"SearchConversions",s.search_conversions},{L"SearchAliases",s.search_aliases},{L"SearchCurrency",s.search_currency},{L"CalculatorDegrees",s.calculator_degrees} };
    for (const auto& [name,value]:values) if (status==ERROR_SUCCESS) status=RegSetValueExW(key.value,name,0,REG_DWORD,reinterpret_cast<const BYTE*>(&value),sizeof(value));
    if (status!=ERROR_SUCCESS) { error=system_error(status); return false; }
    return true;
}
bool set_start_at_login(bool enabled, std::wstring& error,const wchar_t* run_key) {
    RegistryKey key;
    auto status=RegCreateKeyExW(HKEY_CURRENT_USER,run_key,0,nullptr,0,KEY_SET_VALUE,nullptr,&key.value,nullptr);
    if (status==ERROR_SUCCESS) {
        if (enabled) {
            std::array<wchar_t,32768> path{};
            const DWORD count=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));
            if (!count || count>=path.size()) { error=L"Could not find the application path."; return false; }
            const auto command=L"\""+std::wstring(path.data())+L"\" --background";
            status=RegSetValueExW(key.value,L"FastPalette",0,REG_SZ,reinterpret_cast<const BYTE*>(command.c_str()),static_cast<DWORD>((command.size()+1)*sizeof(wchar_t)));
        } else {
            status=RegDeleteValueW(key.value,L"FastPalette");
            if (status==ERROR_FILE_NOT_FOUND) status=ERROR_SUCCESS;
        }
    }
    if (status!=ERROR_SUCCESS) { error=system_error(status); return false; }
    return true;
}
void load_usage(std::vector<AppEntry>& apps) {
    RegistryKey key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\FastPalette\\Usage",0,KEY_QUERY_VALUE,&key.value)!=ERROR_SUCCESS) return;
    for (auto& app:apps) {
        Usage usage{}; DWORD bytes=sizeof(usage);
        if (RegGetValueW(key.value,nullptr,usage_name(app.id).c_str(),RRF_RT_REG_BINARY,nullptr,&usage,&bytes)==ERROR_SUCCESS && bytes==sizeof(usage)) {
            app.use_count=static_cast<std::uint32_t>(std::min<std::uint64_t>(usage.count,1000000)); app.last_used=usage.last;
        }
    }
}
void save_usage(const AppEntry& app) {
    RegistryKey key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\FastPalette\\Usage",0,nullptr,0,KEY_SET_VALUE,nullptr,&key.value,nullptr)!=ERROR_SUCCESS) return;
    const Usage usage{app.use_count,app.last_used};
    RegSetValueExW(key.value,usage_name(app.id).c_str(),0,REG_BINARY,reinterpret_cast<const BYTE*>(&usage),sizeof(usage));
}
std::wstring hotkey_label(const Settings& s) {
    return hotkey_label(Hotkey{s.modifiers,s.key});
}
std::vector<Hotkey> all_hotkeys(const Settings& s) {
    std::vector<Hotkey> result{{s.modifiers,s.key}};
    result.insert(result.end(),s.extra_bindings.begin(),s.extra_bindings.end());return result;
}
std::wstring hotkey_label(const Hotkey& s) {
    std::wstring out;
    if (s.modifiers&MOD_CONTROL) out+=L"Ctrl+";
    if (s.modifiers&MOD_ALT) out+=L"Alt+";
    if (s.modifiers&MOD_SHIFT) out+=L"Shift+";
    if (s.modifiers&MOD_WIN) out+=L"Win+";
    wchar_t name[64]{};
    LONG code=static_cast<LONG>(MapVirtualKeyW(s.key,MAPVK_VK_TO_VSC)<<16);
    if (s.key==VK_LEFT || s.key==VK_RIGHT || s.key==VK_UP || s.key==VK_DOWN || s.key==VK_DELETE || s.key==VK_INSERT || s.key==VK_HOME || s.key==VK_END || s.key==VK_PRIOR || s.key==VK_NEXT) code|=1<<24;
    if (GetKeyNameTextW(code,name,64)) out+=name; else out+=L"Key "+std::to_wstring(s.key);
    return out;
}
}
