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
bool launch_app(const AppEntry& app, std::wstring& error) {
    ComApartment apartment;
    SHELLEXECUTEINFOW info{sizeof(info)};
    info.fMask=SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
    info.nShow=SW_SHOWNORMAL;
    PIDLIST_ABSOLUTE pidl=nullptr;
    if (app.kind==LaunchKind::shell) {
        const auto hr=SHParseDisplayName(app.target.c_str(),nullptr,&pidl,0,nullptr);
        if (FAILED(hr)) { error=system_error(static_cast<DWORD>(hr)); return false; }
        info.fMask|=SEE_MASK_IDLIST; info.lpIDList=pidl;
    } else info.lpFile=app.target.c_str();
    const bool ok=ShellExecuteExW(&info)!=FALSE;
    if (!ok) error=system_error(GetLastError());
    CoTaskMemFree(pidl); return ok;
}
HICON load_app_icon(const AppEntry& app) {
    SHFILEINFOW info{};
    if (app.kind==LaunchKind::shell) {
        PIDLIST_ABSOLUTE pidl=nullptr;
        if (FAILED(SHParseDisplayName(app.target.c_str(),nullptr,&pidl,0,nullptr))) return nullptr;
        SHGetFileInfoW(reinterpret_cast<LPCWSTR>(pidl),0,&info,sizeof(info),SHGFI_PIDL|SHGFI_ICON|SHGFI_SMALLICON);
        CoTaskMemFree(pidl);
    } else SHGetFileInfoW(app.target.c_str(),0,&info,sizeof(info),SHGFI_ICON|SHGFI_SMALLICON);
    return info.hIcon;
}
}
