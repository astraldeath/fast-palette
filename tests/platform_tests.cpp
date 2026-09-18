#include "settings.hpp"
#include "catalog.hpp"
#include "core.hpp"
#include <iostream>
#include <filesystem>
#include <fstream>
#include <shlobj.h>
#include <wrl/client.h>
#include <propkey.h>
#include <propvarutil.h>
using namespace palette;
int main(int argc, char** argv) {
    if (argc > 1) {
        const auto apps = discover_apps();
        std::wcout << L"Discovered " << apps.size() << L" applications\n";
        if (argc > 2) {
            const auto filter = std::filesystem::path(argv[2]).wstring();
            const auto hits = search(apps, filter, apps.size());
            std::wcout << L"Matches for [" << filter << L"]: " << hits.size() << L'\n';
            for (const auto& hit : hits) {
                const auto& app = apps[hit.index];
                std::wcout << L"score=" << hit.score << L" name=[" << app.name << L"] id=["
                           << app.id << L"] kind=" << static_cast<int>(app.kind)
                           << L" target=[" << app.target << L"]\n";
            }
        } else {
            for (size_t i=0; i < apps.size() && i<12; ++i) {
                std::wcout << apps[i].name << L" | " << apps[i].target << L'\n';
            }
        }
        CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
        size_t invalid=0,shell=0;
        for(const auto& app:apps) if(app.kind==LaunchKind::shell) {
            ++shell;PIDLIST_ABSOLUTE item=nullptr;
            if(FAILED(SHParseDisplayName(app.target.c_str(),nullptr,&item,0,nullptr)))++invalid;
            CoTaskMemFree(item);
        }
        CoUninitialize();
        std::wcout << L"Shell identities: " << shell << L", unresolved: " << invalid << L'\n';
        return apps.empty() || invalid ? 1 : 0;
    }
    int failed = 0;
    auto check = [&](bool ok, const char* name) { if (!ok) { ++failed; std::cerr << "FAIL: " << name << '\n'; } };
    check(detail::path_is_within_directory(
              L"C:\\Menu\\Programs\\Startup\\AutorunsDisabled\\alias.lnk",
              L"c:\\menu\\programs\\startup"),
          "startup descendants are excluded case-insensitively");
    check(!detail::path_is_within_directory(
               L"C:\\Menu\\Programs\\Startup Tools\\utility.lnk",
               L"C:\\Menu\\Programs\\Startup"),
          "startup exclusion respects directory boundaries");
    const auto key = L"Software\\FastPaletteTests-" + std::to_wstring(GetCurrentProcessId());
    auto defaults = load_settings(key.c_str());
    check(defaults.left_win && defaults.right_win && !defaults.start_at_login, "safe settings defaults");
    check(defaults.search_apps && defaults.search_calculator && defaults.search_settings && defaults.search_paths && !defaults.search_everything,"source defaults need no external service");
    Settings desired; desired.left_win=false; desired.modifiers=MOD_CONTROL|MOD_SHIFT; desired.key='P';
    desired.extra_bindings={{MOD_ALT,VK_SPACE},{MOD_CONTROL|MOD_ALT,'K'},{MOD_WIN,'R'}};
    desired.search_apps=false;desired.search_calculator=false;desired.search_settings=false;desired.search_paths=false;desired.search_everything=true;
    desired.everything_prefix_only=true;desired.everything_prefix=L"ef";
    desired.automatic_updates=false;
    desired.search_conversions=false;desired.search_aliases=false;desired.calculator_degrees=true;
    desired.aliases={{L"work",L"C:\\Projects",L""},{L"edit",L"notepad.exe",L"\"my notes.txt\""}};
    set_module_rule(desired,Module::apps,{L"launch",true});
    desired.portable_apps={L"C:\\Program Files\\Example\\example.exe", L"C:\\Portable\\éditeur.exe"};
    std::wstring error;
    check(save_settings(desired,error,key.c_str()), "settings save");
    const auto actual=load_settings(key.c_str());
    check(!actual.left_win && actual.right_win && actual.modifiers==(MOD_CONTROL|MOD_SHIFT) && actual.key=='P', "settings roundtrip");
    check(actual.portable_apps==desired.portable_apps, "portable Unicode paths roundtrip");
    check(actual.extra_bindings==desired.extra_bindings, "multiple keybindings roundtrip");
    check(actual.aliases==desired.aliases,"aliases including empty arguments roundtrip");
    check(actual.prefixes==desired.prefixes,"module prefixes roundtrip");
    check(!actual.search_conversions && !actual.search_aliases && actual.calculator_degrees,"new module and angle preferences roundtrip");
    check(!actual.search_apps && !actual.search_calculator && !actual.search_settings && !actual.search_paths && actual.search_everything,"independent source toggles roundtrip");
    check(actual.everything_prefix_only && actual.everything_prefix==L"ef","Everything prefix options roundtrip");
    check(defaults.automatic_updates && !actual.automatic_updates,"automatic update preference persists");
    const auto legacy_path=key+L"-legacy";HKEY legacy_key=nullptr;
    RegCreateKeyExW(HKEY_CURRENT_USER,legacy_path.c_str(),0,nullptr,0,KEY_SET_VALUE,nullptr,&legacy_key,nullptr);
    const wchar_t legacy_prefix[]=L"app";DWORD only=1;
    RegSetValueExW(legacy_key,L"EverythingPrefix",0,REG_SZ,reinterpret_cast<const BYTE*>(legacy_prefix),sizeof(legacy_prefix));
    RegSetValueExW(legacy_key,L"EverythingPrefixOnly",0,REG_DWORD,reinterpret_cast<const BYTE*>(&only),sizeof(only));RegCloseKey(legacy_key);
    auto migrated=load_settings(legacy_path.c_str());
    check(migrated.everything_prefix==L"app" && migrated.everything_prefix_only && module_rule(migrated,Module::apps).prefix!=L"app","legacy Everything prefix survives new defaults");
    migrated.everything_prefix.clear();migrated.everything_prefix_only=false;
    check(save_settings(migrated,error,legacy_path.c_str()) && load_settings(legacy_path.c_str()).everything_prefix.empty(),"optional Everything prefix can be cleared");
    RegDeleteTreeW(HKEY_CURRENT_USER,legacy_path.c_str());
    check(!valid_everything_prefix(L"") && !valid_everything_prefix(L"a b") && !valid_everything_prefix(L"="),"invalid prefixes rejected");
    check(!valid_hotkey(0,'A') && !valid_hotkey(MOD_CONTROL,VK_F12) && !valid_hotkey(MOD_WIN,'L'), "reject unmodified and reserved combinations");
    check(valid_hotkey(MOD_CONTROL|MOD_ALT,VK_SPACE), "accept configured combination");
    check(valid_hotkey(MOD_WIN|MOD_SHIFT,'P'), "accept Windows modifier combinations");
    check(valid_hotkey(MOD_WIN,'R'), "accept Windows Run replacement binding");
    desired.key=0;
    check(!save_settings(desired,error,key.c_str()), "reject invalid settings before persistence");
    check(load_settings(key.c_str()).key=='P', "invalid save preserves working config");
    desired.key='P'; desired.extra_bindings.push_back({desired.modifiers,desired.key});
    check(!save_settings(desired,error,key.c_str()), "duplicate hotkeys rejected");
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    const auto temp=std::filesystem::temp_directory_path()/(L"FastPaletteTest-"+std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(temp);
    auto make_link=[&](const wchar_t* name,const wchar_t* args) {
        Microsoft::WRL::ComPtr<IShellLinkW> link;
        HRESULT hr=CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&link));
        check(SUCCEEDED(hr),"create real shell shortcut fixture");
        if(FAILED(hr))return std::wstring{};
        link->SetPath(L"C:\\Windows\\System32\\notepad.exe");link->SetArguments(args);
        Microsoft::WRL::ComPtr<IPropertyStore> props;link.As(&props);
        PROPVARIANT id{};InitPropVariantFromString(L"FastPalette.Test.SharedAppId",&id);props->SetValue(PKEY_AppUserModel_ID,id);props->Commit();PropVariantClear(&id);
        Microsoft::WRL::ComPtr<IPersistFile> file;link.As(&file);
        const auto path=(temp/name).native();check(SUCCEEDED(file->Save(path.c_str(),TRUE)),"save shell fixture");return path;
    };
    const auto normal=make_link(L"normal.lnk",L"normal.txt"),special=make_link(L"special.lnk",L"special.txt");
    check(shortcut_identity(normal)!=shortcut_identity(special),"shared AppUserModelID must preserve distinct launch arguments");
    {
        wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
        const auto probe=std::filesystem::path(executable).parent_path()/L"launch_probe.exe";
        const auto output=temp/L"result with spaces.txt";
        const auto shortcut=temp/L"probe.lnk";
        Microsoft::WRL::ComPtr<IShellLinkW> link;
        CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&link));
        link->SetPath(probe.c_str());link->SetWorkingDirectory(temp.c_str());
        const auto arguments=L"\""+output.native()+L"\" \"alpha beta\"";
        link->SetArguments(arguments.c_str());
        Microsoft::WRL::ComPtr<IPersistFile> file;link.As(&file);file->Save(shortcut.c_str(),TRUE);
        AppEntry app;app.target=shortcut.native();app.kind=LaunchKind::shortcut;
        check(launch_app(app,error),"shell launches shortcut fixture");
        for(int attempt=0;attempt<100 && !std::filesystem::exists(output);++attempt)Sleep(20);
        std::wstring actual_output;
        for(int attempt=0;attempt<50;++attempt) {
            HANDLE result=CreateFileW(output.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(result!=INVALID_HANDLE_VALUE) {
                const DWORD bytes=GetFileSize(result,nullptr);actual_output.resize(bytes/sizeof(wchar_t));DWORD read=0;
                if(bytes)ReadFile(result,actual_output.data(),bytes,&read,nullptr);CloseHandle(result);break;
            }
            Sleep(20);
        }
        check(actual_output==L"alpha beta\n"+temp.native(),"shortcut launch preserves arguments and working directory");
        std::filesystem::remove(output);std::filesystem::remove(shortcut);
    }
    std::filesystem::remove(normal);std::filesystem::remove(special);std::filesystem::remove(temp);
    CoUninitialize();
    RegDeleteTreeW(HKEY_CURRENT_USER,key.c_str());
    std::wcout << L"Platform checks: " << (failed ? L"FAILED" : L"passed") << L'\n';
    return failed ? 1 : 0;
}
