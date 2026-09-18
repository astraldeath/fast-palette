#include "catalog.hpp"
#include "win_util.hpp"
#include <filesystem>
#include <iostream>
#include <shlobj.h>
#include <wrl/client.h>

using namespace palette;
int main() {
    int failed=0;
    auto check=[&](bool ok,const char* name) {
        if(!ok) { ++failed;std::cerr<<"FAIL: "<<name<<'\n'; }
    };
    AppEntry app;app.target=L"C:\\Example\\tool.exe";app.kind=LaunchKind::executable;
    check(resolved_app_path(app)==app.target,"executable path is preserved");
    check(supports_action(app,ResultAction::run_as_admin),"executables support elevation");
    check(supports_action(app,ResultAction::open_folder),"files support reveal");
    app.target=L"notepad.exe";
    const auto notepad=resolved_app_path(app);
    check(!notepad.empty() && std::filesystem::path(notepad).is_absolute(),"bare executable alias resolves using Windows search path");
    app.target=L"%APPDATA%";app.is_folder=true;
    const auto appdata=resolved_app_path(app);
    check(!appdata.empty() && appdata.find(L'%')==std::wstring::npos,"environment variables expand before path validation");
    check(supports_action(app,ResultAction::open_folder),"environment directory alias supports reveal");
    app.is_folder=false;
    app.target=L"C:\\Example\\document.txt";
    check(!supports_action(app,ResultAction::run_as_admin),"documents cannot elevate");
    std::wstring error;
    check(!launch_app(app,error,ResultAction::run_as_admin) && !error.empty(),"document elevation rejected without launching");
    app.target=L"ms-settings:display";
    check(resolved_app_path(app).empty(),"settings URI has no filesystem path");
    check(!supports_action(app,ResultAction::open_folder),"settings cannot reveal");
    check(!launch_app(app,error,ResultAction::open_folder) && !error.empty(),"URI reveal rejected without launching");
    app.target=L"shell:AppsFolder\\Example";app.kind=LaunchKind::shell;
    check(resolved_app_path(app).empty(),"shell results have no filesystem path");
    check(!supports_action(app,ResultAction::run_as_admin),"shell results cannot elevate");
    app.kind=LaunchKind::shortcut;app.target=L"C:\\Example\\missing.lnk";
    check(resolved_app_path(app).empty(),"unreadable shortcut never copies its own path");

    ComApartment apartment;
    wchar_t module[32768]{};
    GetModuleFileNameW(nullptr,module,32768);
    const auto shortcut=std::filesystem::temp_directory_path()/
        (L"FastPalette-result-action-"+std::to_wstring(GetCurrentProcessId())+L".lnk");
    Microsoft::WRL::ComPtr<IShellLinkW> link;
    Microsoft::WRL::ComPtr<IPersistFile> file;
    bool saved=SUCCEEDED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&link)));
    if(saved) {
        saved=SUCCEEDED(link->SetPath(module)) && SUCCEEDED(link->SetArguments(L"--example \"two words\"")) &&
            SUCCEEDED(link.As(&file)) && SUCCEEDED(file->Save(shortcut.c_str(),TRUE));
    }
    check(saved,"create isolated shortcut fixture");
    if(saved) {
        app.target=shortcut.native();
        check(resolved_app_path(app)==module,"shortcut resolves actual target");
        check(supports_action(app,ResultAction::run_as_admin),"executable shortcut supports elevation");
        check(supports_action(app,ResultAction::open_folder),"executable shortcut supports reveal");
        std::filesystem::remove(shortcut);
    }
    return failed?1:0;
}
