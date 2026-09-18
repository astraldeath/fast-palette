#include "catalog.hpp"
#include "core.hpp"
#include "settings.hpp"
#include "win_util.hpp"
#include <shlobj.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <propkey.h>
#include <propvarutil.h>
#include <wrl/client.h>
#include <filesystem>
#include <unordered_set>
#include <algorithm>

namespace palette {
using Microsoft::WRL::ComPtr;
namespace detail {
bool path_is_within_directory(std::wstring_view path, std::wstring_view directory) {
    auto normalized_path=fold(std::filesystem::path(path).lexically_normal().native());
    auto normalized_directory=fold(std::filesystem::path(directory).lexically_normal().native());
    if(normalized_path==normalized_directory)return true;
    if(normalized_directory.empty())return false;
    const auto last=normalized_directory.back();
    if(last!=L'\\' && last!=L'/')normalized_directory.push_back(L'\\');
    return normalized_path.starts_with(normalized_directory);
}
}
namespace {
std::wstring property(IShellItem2* item, REFPROPERTYKEY key) {
    PWSTR value=nullptr;
    if (FAILED(item->GetString(key,&value))) return {};
    std::wstring result=value; CoTaskMemFree(value); return result;
}
std::wstring command_identity(const std::wstring& path,const std::wstring& args) {
    wchar_t expanded[32768]{};
    const auto n=ExpandEnvironmentStringsW(path.c_str(),expanded,32768);
    return L"exe:"+fold(std::filesystem::path(n && n<=32768?expanded:path).lexically_normal().native())+L"|"+args;
}
struct ResolvedTarget { std::wstring path, arguments, directory; };
std::wstring expanded_path(const std::wstring& path) {
    wchar_t expanded[32768]{};
    const auto count=ExpandEnvironmentStringsW(path.c_str(),expanded,32768);
    return count && count<=32768?std::wstring(expanded):path;
}
ResolvedTarget resolve_target(const AppEntry& app) {
    if(app.kind==LaunchKind::shell || app.target.empty())return {};
    const auto folded=fold(app.target);
    if(folded.starts_with(L"ms-settings:") || folded.starts_with(L"shell:"))return {};
    ResolvedTarget result{expanded_path(app.target),app.arguments,{}};
    if(!std::filesystem::path(result.path).is_absolute()) {
        // Resolve executable aliases using Windows' executable search path,
        // without parsing a command line or invoking a command interpreter.
        if(std::filesystem::path(result.path).has_parent_path())return {};
        wchar_t found[32768]{};
        const auto count=SearchPathW(nullptr,result.path.c_str(),L".exe",32768,found,nullptr);
        if(!count || count>=32768)return {};
        result.path=found;
    }
    if(fold(std::filesystem::path(result.path).extension().native())==L".lnk") {
        ComPtr<IShellLinkW> link;
        ComPtr<IPersistFile> file;
        if(FAILED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&link))) ||
            FAILED(link.As(&file)) || FAILED(file->Load(result.path.c_str(),STGM_READ)))return {};
        wchar_t path[32768]{},arguments[32768]{},directory[32768]{};
        if(FAILED(link->GetPath(path,32768,nullptr,0)) || !*path)return {};
        link->GetArguments(arguments,32768);
        link->GetWorkingDirectory(directory,32768);
        result={path,arguments,directory};
        if(!app.arguments.empty()) {
            if(!result.arguments.empty())result.arguments+=L" ";
            result.arguments+=app.arguments;
        }
    }
    result.path=expanded_path(result.path);
    result.directory=expanded_path(result.directory);
    if(!std::filesystem::path(result.path).is_absolute())return {};
    return result;
}
bool executable_path(const std::wstring& path) {
    const auto extension=fold(std::filesystem::path(path).extension().native());
    return extension==L".exe" || extension==L".com";
}
}
std::wstring resolved_app_path(const AppEntry& app) {
    ComApartment apartment;
    return resolve_target(app).path;
}
bool supports_action(const AppEntry& app,ResultAction action) {
    if(action==ResultAction::open)return !app.target.empty();
    const auto path=resolved_app_path(app);
    if(path.empty())return false;
    return action==ResultAction::open_folder || (!app.is_folder && executable_path(path));
}
std::wstring shortcut_identity(const std::wstring& path,std::wstring* command) {
    ComPtr<IShellLinkW> link;
    if (FAILED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&link)))) return fold(path);
    ComPtr<IPersistFile> file;
    if (FAILED(link.As(&file)) || FAILED(file->Load(path.c_str(),STGM_READ))) return fold(path);
    // AppUserModelID groups windows, not launch profiles: arguments and working
    // directories must remain part of a desktop shortcut's identity.
    wchar_t target[32768]{}, args[32768]{}, directory[32768]{};
    link->GetPath(target,32768,nullptr,SLGP_RAWPATH);
    link->GetArguments(args,32768); link->GetWorkingDirectory(directory,32768);
    if (!*target) return fold(path);
    const auto identity=command_identity(target,args);
    if(command)*command=identity;
    return identity+L"|"+fold(directory);
}
std::vector<AppEntry> discover_apps(std::stop_token stop) {
    ComApartment apartment;
    std::vector<AppEntry> apps;
    std::unordered_set<std::wstring> seen;
    std::unordered_set<std::wstring> desktop_commands;
    std::vector<std::wstring> startup_roots;
    for(const auto& folder:{FOLDERID_Startup,FOLDERID_CommonStartup}) {
        PWSTR path=nullptr;
        if(SUCCEEDED(SHGetKnownFolderPath(folder,0,nullptr,&path))) {
            startup_roots.emplace_back(path);
            CoTaskMemFree(path);
        }
    }
    auto add=[&](AppEntry entry) {
        if (entry.name.empty() || entry.target.empty() || !seen.insert(entry.id).second) return;
        entry.folded=fold(entry.name); apps.push_back(std::move(entry));
    };
    for (const auto& folder:{FOLDERID_Programs,FOLDERID_CommonPrograms}) {
        PWSTR path=nullptr;
        if (FAILED(SHGetKnownFolderPath(folder,0,nullptr,&path))) continue;
        const std::filesystem::path root(path); CoTaskMemFree(path);
        std::error_code ec;
        std::filesystem::recursive_directory_iterator it(root,std::filesystem::directory_options::skip_permission_denied,ec), end;
        for (;it!=end && !stop.stop_requested();it.increment(ec)) {
            if (ec) { ec.clear(); continue; }
            const auto entry=it->path(); const auto ext=fold(entry.extension().native());
            const bool startup=std::any_of(startup_roots.begin(),startup_roots.end(),[&](const auto& excluded) {
                return detail::path_is_within_directory(entry.native(),excluded);
            });
            if(startup) {
                if(it->is_directory(ec))it.disable_recursion_pending();
                ec.clear();continue;
            }
            if (!it->is_regular_file(ec)) continue;
            if (ext!=L".lnk" && ext!=L".url" && ext!=L".appref-ms") continue;
            AppEntry app; app.name=entry.stem().native(); app.target=entry.native();
            std::wstring command;
            app.id=ext==L".lnk" ? shortcut_identity(entry.native(),&command) : fold(app.target);
            if(!command.empty())desktop_commands.insert(command);
            app.detail=L"Application"; app.kind=LaunchKind::shortcut; add(std::move(app));
        }
    }
    ComPtr<IShellItem> folderItem;
    ComPtr<IShellFolder> folder;
    if (!stop.stop_requested() && SUCCEEDED(SHGetKnownFolderItem(FOLDERID_AppsFolder,KF_FLAG_DEFAULT,nullptr,IID_PPV_ARGS(&folderItem))) &&
        SUCCEEDED(folderItem->BindToHandler(nullptr,BHID_SFObject,IID_PPV_ARGS(&folder)))) {
        ComPtr<IEnumIDList> enumeration;
        if (SUCCEEDED(folder->EnumObjects(nullptr,SHCONTF_NONFOLDERS,&enumeration)) && enumeration) {
            PITEMID_CHILD child=nullptr;
            while (!stop.stop_requested() && enumeration->Next(1,&child,nullptr)==S_OK) {
                ComPtr<IShellItem2> item;
                if (SUCCEEDED(SHCreateItemWithParent(nullptr,folder.Get(),child,IID_PPV_ARGS(&item)))) {
                    PWSTR name=nullptr, parsing=nullptr;
                    if (SUCCEEDED(item->GetDisplayName(SIGDN_NORMALDISPLAY,&name)) && SUCCEEDED(item->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING,&parsing))) {
                        AppEntry app; app.name=name; app.target=L"shell:AppsFolder\\"+std::wstring(parsing); app.kind=LaunchKind::shell; app.detail=L"Application";
                        const auto appId=property(item.Get(),PKEY_AppUserModel_ID);
                        app.id=appId.empty() ? L"shell:"+fold(app.target) : L"app:"+fold(appId);
                        const auto link=property(item.Get(),PKEY_Link_TargetParsingPath);
                        const auto args=property(item.Get(),PKEY_Link_Arguments);
                        if (!link.empty() && fold(std::filesystem::path(link).extension().native())==L".lnk") app.id=shortcut_identity(link);
                        if(link.empty() || !desktop_commands.contains(command_identity(link,args)))add(std::move(app));
                    }
                    CoTaskMemFree(name); CoTaskMemFree(parsing);
                }
                CoTaskMemFree(child); child=nullptr;
            }
        }
    }
    for (const auto& path:load_settings().portable_apps) {
        AppEntry app; app.name=std::filesystem::path(path).stem().native(); app.target=path;
        app.id=L"portable:"+fold(path); app.kind=LaunchKind::executable; app.detail=L"Portable application"; add(std::move(app));
    }
    std::sort(apps.begin(),apps.end(),[](const auto& a,const auto& b){ return a.folded<b.folded; });
    load_usage(apps); return apps;
}
bool launch_app(const AppEntry& app, std::wstring& error, ResultAction action) {
    ComApartment apartment;
    error.clear();
    ResolvedTarget resolved;
    if(action!=ResultAction::open) {
        resolved=resolve_target(app);
        if(resolved.path.empty()) {
            error=L"This result does not have a filesystem path for this action.";
            return false;
        }
        if(action==ResultAction::run_as_admin && (app.is_folder || !executable_path(resolved.path))) {
            error=L"Run as administrator is available only for executable files and shortcuts to executables.";
            return false;
        }
        if(action==ResultAction::open_folder) {
            PIDLIST_ABSOLUTE item=nullptr;
            auto hr=SHParseDisplayName(resolved.path.c_str(),nullptr,&item,0,nullptr);
            if(SUCCEEDED(hr))hr=SHOpenFolderAndSelectItems(item,0,nullptr,0);
            CoTaskMemFree(item);
            if(FAILED(hr))error=system_error(static_cast<DWORD>(hr));
            return SUCCEEDED(hr);
        }
    }
    SHELLEXECUTEINFOW info{sizeof(info)};
    info.fMask=SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
    info.nShow=SW_SHOWNORMAL;
    PIDLIST_ABSOLUTE pidl=nullptr;
    const auto launch_target=expanded_path(app.target);
    if(action==ResultAction::run_as_admin) {
        info.lpVerb=L"runas";
        info.lpFile=resolved.path.c_str();
        info.lpParameters=resolved.arguments.empty()?nullptr:resolved.arguments.c_str();
        info.lpDirectory=resolved.directory.empty()?nullptr:resolved.directory.c_str();
    } else if (app.kind==LaunchKind::shell) {
        const auto hr=SHParseDisplayName(app.target.c_str(),nullptr,&pidl,0,nullptr);
        if (FAILED(hr)) { error=system_error(static_cast<DWORD>(hr)); return false; }
        info.fMask|=SEE_MASK_IDLIST; info.lpIDList=pidl;
    } else {
        info.lpFile=launch_target.c_str();
        info.lpParameters=app.arguments.empty()?nullptr:app.arguments.c_str();
    }
    const bool ok=ShellExecuteExW(&info)!=FALSE;
    if (!ok) error=system_error(GetLastError());
    CoTaskMemFree(pidl); return ok;
}
HICON load_app_icon(const AppEntry& app) {
    SHFILEINFOW info{};
    if (app.kind==LaunchKind::shell) {
        PIDLIST_ABSOLUTE pidl=nullptr;
        if (FAILED(SHParseDisplayName(app.target.c_str(),nullptr,&pidl,0,nullptr))) return nullptr;
        SHGetFileInfoW(reinterpret_cast<LPCWSTR>(pidl),0,&info,sizeof(info),SHGFI_PIDL|SHGFI_ICON|SHGFI_LARGEICON);
        CoTaskMemFree(pidl);
    } else {
        const auto path=app.source==SearchSource::alias?resolved_app_path(app):app.target;
        SHGetFileInfoW(path.c_str(),0,&info,sizeof(info),SHGFI_ICON|SHGFI_LARGEICON);
    }
    return info.hIcon;
}
}
