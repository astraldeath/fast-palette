#include "providers.hpp"
#include "core.hpp"
#include "modules.hpp"
#include "win_util.hpp"
#include <shlobj.h>
#include <algorithm>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <optional>

namespace palette {
EverythingQuery route_everything(std::wstring_view query,const Settings& settings) {
    const auto route=route_query(query,settings);
    const bool nonempty=std::any_of(route.text.begin(),route.text.end(),[](wchar_t c){return !iswspace(c);});
    return {route.allows(Module::everything) && nonempty,route.exclusive==Module::everything,route.text};
}
const std::vector<AppEntry>& windows_settings() {
    // URI reference: learn.microsoft.com/windows/apps/develop/launch/launch-settings
    static const auto entries=[] {
        const std::pair<const wchar_t*,const wchar_t*> pages[]={
            {L"Display",L"display"},{L"Night light",L"nightlight"},{L"HDR",L"display-hdr"},
            {L"Graphics",L"display-advancedgraphics"},{L"Sound",L"sound"},{L"Volume mixer",L"apps-volume"},
            {L"Notifications",L"notifications"},{L"Do not disturb",L"quiethours"},{L"Focus",L"quietmomentshome"},
            {L"Power and battery",L"powersleep"},{L"Storage",L"storagesense"},{L"Storage cleanup",L"storagepolicies"},
            {L"Multitasking",L"multitasking"},{L"Optional features",L"optionalfeatures"},
            {L"Recovery",L"recovery"},{L"Activation",L"activation"},{L"Troubleshoot",L"troubleshoot"},
            {L"Remote Desktop",L"remotedesktop"},{L"About this PC",L"about"},{L"Clipboard",L"clipboard"},
            {L"Bluetooth and devices",L"bluetooth"},{L"Printers and scanners",L"printers"},
            {L"Mouse",L"mousetouchpad"},{L"Touchpad",L"devices-touchpad"},{L"Typing",L"typing"},
            {L"AutoPlay",L"autoplay"},{L"USB devices",L"usb"},{L"Cameras",L"camera"},
            {L"Network and internet",L"network-status"},{L"Wi-Fi",L"network-wifi"},
            {L"Ethernet",L"network-ethernet"},{L"VPN",L"network-vpn"},{L"Proxy",L"network-proxy"},
            {L"Airplane mode",L"network-airplanemode"},{L"Mobile hotspot",L"network-mobilehotspot"},
            {L"Advanced network settings",L"network-advancedsettings"},{L"Data usage",L"datausage"},
            {L"Desktop background wallpaper",L"personalization-background"},{L"Colors and dark mode",L"colors"},
            {L"Themes",L"themes"},{L"Lock screen",L"lockscreen"},{L"Start menu",L"personalization-start"},
            {L"Taskbar",L"taskbar"},{L"Fonts",L"fonts"},{L"Installed apps",L"appsfeatures"},
            {L"Default apps",L"defaultapps"},{L"Startup apps",L"startupapps"},
            {L"Your account info",L"yourinfo"},{L"Sign-in options",L"signinoptions"},
            {L"Passkeys",L"passkeys"},{L"Email and accounts",L"emailandaccounts"},
            {L"Other users",L"otherusers"},{L"Access work or school",L"workplace"},
            {L"Windows backup",L"backup"},{L"Date and time",L"dateandtime"},
            {L"Language and region",L"regionlanguage"},{L"Speech",L"speech"},
            {L"Game Mode",L"gaming-gamemode"},{L"Game captures",L"gaming-gamedvr"},
            {L"Accessibility",L"easeofaccess"},{L"Text size",L"easeofaccess-display"},
            {L"Magnifier",L"easeofaccess-magnifier"},{L"Color filters",L"easeofaccess-colorfilter"},
            {L"Contrast themes",L"easeofaccess-highcontrast"},{L"Narrator",L"easeofaccess-narrator"},
            {L"Closed captions",L"easeofaccess-closedcaptioning"},{L"Keyboard accessibility",L"easeofaccess-keyboard"},
            {L"Mouse pointer",L"easeofaccess-mousepointer"},{L"Windows Security",L"windowsdefender"},
            {L"Find my device",L"findmydevice"},{L"For developers",L"developers"},
            {L"Privacy",L"privacy"},{L"Location permissions",L"privacy-location"},
            {L"Camera permissions",L"privacy-webcam"},{L"Microphone permissions",L"privacy-microphone"},
            {L"Diagnostics and feedback",L"privacy-feedback"},{L"Search permissions",L"search-permissions"},
            {L"Searching Windows",L"cortana-windowssearch"},{L"Windows Update",L"windowsupdate"},
            {L"Windows Update history",L"windowsupdate-history"},{L"Windows Update advanced options",L"windowsupdate-options"},
            {L"Delivery Optimization",L"delivery-optimization"},{L"Windows Insider Program",L"windowsinsider"}
        };
        std::vector<AppEntry> result;result.reserve(std::size(pages));
        for(const auto& [name,uri]:pages){AppEntry entry;entry.target=L"ms-settings:"+std::wstring(uri);
            entry.id=entry.target;entry.name=name;entry.detail=L"Windows Settings";
            entry.kind=LaunchKind::executable;entry.source=SearchSource::windows_settings;entry.folded=fold(name);
            result.push_back(std::move(entry));}
        return result;
    }();
    return entries;
}

std::wstring expand_path_query(std::wstring_view query) {
    while(!query.empty() && iswspace(query.front()))query.remove_prefix(1);
    while(!query.empty() && iswspace(query.back()))query.remove_suffix(1);
    if(query.size()>=2 && query.front()==L'"' && query.back()==L'"')query=query.substr(1,query.size()-2);
    if(query.empty() || query.size()>32760)return {};
    std::wstring input(query);
    if(input==L"~" || input.starts_with(L"~\\") || input.starts_with(L"~/"))input.replace(0,1,L"%USERPROFILE%");
    const DWORD size=ExpandEnvironmentStringsW(input.c_str(),nullptr,0);
    if(size==0 || size>32768)return {};
    std::wstring expanded(size,L'\0');
    if(ExpandEnvironmentStringsW(input.c_str(),expanded.data(),size)!=size)return {};
    expanded.pop_back();
    if(expanded.find(L'%')!=std::wstring::npos || expanded.find(L'"')!=std::wstring::npos)return {};
    std::replace(expanded.begin(),expanded.end(),L'/',L'\\');
    const bool drive=expanded.size()>=3 && iswalpha(expanded[0]) && expanded[1]==L':' && expanded[2]==L'\\';
    const bool unc=expanded.starts_with(L"\\\\") && !expanded.starts_with(L"\\\\.\\") && !expanded.starts_with(L"\\\\?\\");
    if(!drive && !unc)return {};
    return expanded;
}

namespace {
std::wstring_view trim_query(std::wstring_view text) {
    while(!text.empty() && iswspace(text.front()))text.remove_prefix(1);
    while(!text.empty() && iswspace(text.back()))text.remove_suffix(1);
    return text;
}
bool command_extension(std::wstring_view extension) {
    return extension==L".exe" || extension==L".com" || extension==L".msc" || extension==L".cpl" ||
           extension==L".cmd" || extension==L".bat";
}
std::wstring search_command(const std::wstring& directories,const std::wstring& name,const std::wstring& extension) {
    if(directories.empty())return {};
    // Try console and Control Panel tools first: services.exe is the service manager process, not its UI.
    constexpr const wchar_t* suffixes[]={L".msc",L".cpl",L".exe",L".com",L".cmd",L".bat"};
    wchar_t found[32768]{};
    for(const auto* suffix:suffixes){
        if(!extension.empty() && extension!=suffix)continue;
        const auto file=extension.empty()?name+suffix:name;
        const auto length=SearchPathW(directories.c_str(),file.c_str(),nullptr,static_cast<DWORD>(std::size(found)),found,nullptr);
        if(length && length<std::size(found)){
            const auto attributes=GetFileAttributesW(found);
            if(attributes!=INVALID_FILE_ATTRIBUTES && !(attributes&FILE_ATTRIBUTE_DIRECTORY))return found;
        }
    }
    return {};
}
std::wstring registered_command(const std::wstring& name,const std::wstring& extension) {
    if(!extension.empty() && extension!=L".exe")return {};
    const auto key=L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\"+name+(extension.empty()?L".exe":L"");
    for(auto hive:{HKEY_CURRENT_USER,HKEY_LOCAL_MACHINE})for(auto view:{RRF_SUBKEY_WOW6464KEY,RRF_SUBKEY_WOW6432KEY}){
        wchar_t value[32768]{};DWORD bytes=sizeof(value);
        if(RegGetValueW(hive,key.c_str(),nullptr,RRF_RT_REG_SZ|RRF_RT_REG_EXPAND_SZ|view,nullptr,value,&bytes)!=ERROR_SUCCESS)continue;
        auto target=expand_path_query(value);
        if(target.empty() || fold(std::filesystem::path(target).extension().native())!=L".exe")continue;
        const auto attributes=GetFileAttributesW(target.c_str());
        if(attributes!=INVALID_FILE_ATTRIBUTES && !(attributes&FILE_ATTRIBUTE_DIRECTORY))return target;
    }
    return {};
}
std::wstring resolve_run_name(const std::wstring& name) {
    if(name.empty() || name.size()>255 || name.back()==L'.' || name.back()==L' ' ||
       name.find_first_of(L"<>:\"/\\|?*%")!=name.npos)return {};
    const auto extension=fold(std::filesystem::path(name).extension().native());
    if(!extension.empty() && !command_extension(extension))return {};
    wchar_t directory[MAX_PATH]{};std::wstring system;
    auto length=GetSystemDirectoryW(directory,MAX_PATH);if(length && length<MAX_PATH)system=directory;
    length=GetWindowsDirectoryW(directory,MAX_PATH);if(length && length<MAX_PATH){if(!system.empty())system+=L';';system+=directory;}
    if(auto found=search_command(system,name,extension);!found.empty())return found;
    if(auto found=registered_command(name,extension);!found.empty())return found;
    wchar_t environment[32768]{};
    length=GetEnvironmentVariableW(L"PATH",environment,static_cast<DWORD>(std::size(environment)));
    if(!length || length>=std::size(environment))return {};
    std::wstring paths;std::wstring_view remaining(environment,length);
    while(!remaining.empty()){
        const auto separator=remaining.find(L';');const auto path=expand_path_query(remaining.substr(0,separator));
        if(!path.empty() && !path.starts_with(L"\\\\") && path.find(L';')==path.npos){if(!paths.empty())paths+=L';';paths+=path;}
        if(separator==remaining.npos)break;
        remaining.remove_prefix(separator+1);
    }
    return search_command(paths,name,extension);
}
std::vector<AppEntry> path_entry(std::wstring target,std::wstring arguments={}) {
    if(target.empty())return {};
    const auto attributes=GetFileAttributesW(target.c_str());if(attributes==INVALID_FILE_ATTRIBUTES)return {};
    const bool folder=(attributes&FILE_ATTRIBUTE_DIRECTORY)!=0;
    if(!arguments.empty() && (folder || !command_extension(fold(std::filesystem::path(target).extension().native()))))return {};
    AppEntry entry;entry.id=L"path:"+fold(target);entry.name=std::filesystem::path(target).filename().native();
    if(entry.name.empty())entry.name=target;
    if(!arguments.empty()){entry.name+=L" "+arguments;entry.id+=L"|"+arguments;}
    entry.detail=target;entry.target=std::move(target);entry.arguments=std::move(arguments);
    entry.kind=LaunchKind::executable;entry.source=SearchSource::path;entry.is_folder=folder;
    return {std::move(entry)};
}
}

std::vector<AppEntry> path_results(std::wstring_view query) {
    query=trim_query(query);
    if(query.empty() || query.size()>32760 || std::any_of(query.begin(),query.end(),[](wchar_t c){return iswcntrl(c) && c!=L'\t';}))return {};
    const auto folded=fold(query);
    if(folded.starts_with(L"ms-settings:") || folded.starts_with(L"shell:")){
        AppEntry entry;entry.target=query;entry.id=L"path:"+folded;entry.name=query;
        if(folded.starts_with(L"shell:")){
            if(query.size()==6)return {};
            ComApartment apartment;PIDLIST_ABSOLUTE item=nullptr;SFGAOF attributes=0;
            const auto result=SHParseDisplayName(entry.target.c_str(),nullptr,&item,SFGAO_FOLDER,&attributes);
            CoTaskMemFree(item);if(FAILED(result))return {};
            entry.kind=LaunchKind::shell;entry.is_folder=(attributes&SFGAO_FOLDER)!=0;
        } else {
            const auto slug=std::wstring_view(folded).substr(12);
            if(slug.find_first_not_of(L"abcdefghijklmnopqrstuvwxyz0123456789-")!=slug.npos)return {};
            entry.kind=LaunchKind::executable;entry.detail=L"Windows Settings";
        }
        entry.source=SearchSource::path;return {std::move(entry)};
    }
    // An existing full path wins over interpreting spaces as argument separators.
    if(auto direct=path_entry(expand_path_query(query));!direct.empty())return direct;
    const bool quoted=query.front()==L'"';
    const auto end=quoted?query.find(L'"',1):query.find_first_of(L" \t");
    if(quoted && (end==query.npos || (end+1<query.size() && !iswspace(query[end+1]))))return {};
    const auto name=quoted?query.substr(1,end-1):query.substr(0,end);
    const auto args=end==query.npos?std::wstring_view{}:trim_query(query.substr(end+(quoted?1:0)));
    auto target=expand_path_query(name);
    if(target.empty())target=resolve_run_name(std::wstring(name));
    else if(!args.empty() && !command_extension(fold(std::filesystem::path(target).extension().native())))return {};
    return path_entry(std::move(target),std::wstring(args));
}

// Everything's documented Unicode IPC v1 format uses DWORD offsets, including on x64.
// https://www.voidtools.com/support/everything/sdk/ipc/
std::vector<AppEntry> parse_everything_reply(std::span<const std::byte> bytes) {
    if(bytes.size()<28 || bytes.size()>1024*1024)return {};
    const auto dword=[&](size_t offset){DWORD value;std::memcpy(&value,bytes.data()+offset,4);return value;};
    const auto count=dword(20);if(count>7 || count>(bytes.size()-28)/12)return {};
    const size_t text_start=28+12*count;
    const auto string_at=[&](DWORD offset)->std::optional<std::wstring>{
        if(offset<text_start || offset%2 || offset>=bytes.size())return {};
        std::wstring result;
        for(size_t pos=offset;pos+2<=bytes.size();pos+=2){wchar_t value;std::memcpy(&value,bytes.data()+pos,2);
            if(!value)return result;if(result.size()>=32767)return {};result.push_back(value);}
        return {};
    };
    std::vector<AppEntry> result;
    for(DWORD i=0;i<count;++i){const auto name=string_at(dword(28+12*i+4)),path=string_at(dword(28+12*i+8));
        if(!name || !path || name->empty())return {};
        const auto full=path->empty()?*name:*path+(path->back()==L'\\'?L"":L"\\")+*name;
        if(expand_path_query(full).empty())continue;
        AppEntry entry;entry.id=L"path:"+fold(full);entry.name=*name;entry.target=full;entry.detail=full;
        entry.kind=LaunchKind::executable;entry.source=SearchSource::file;entry.is_folder=(dword(28+12*i)&1)!=0;result.push_back(std::move(entry));}
    return result;
}

HWND everything_window() {
    auto window=FindWindowW(L"EVERYTHING_TASKBAR_NOTIFICATION",nullptr);
    if(!window)window=FindWindowW(L"EVERYTHING_TASKBAR_NOTIFICATION_(1.5a)",nullptr);
    return window;
}

namespace {
constexpr ULONG_PTR reply_id=0x46505331;
struct ReplyState { bool received=false; EverythingReply reply; HWND server=nullptr; };
LRESULT CALLBACK reply_proc(HWND window,UINT message,WPARAM w,LPARAM l) {
    auto* state=reinterpret_cast<ReplyState*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){state=static_cast<ReplyState*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(state));}
    if(message==WM_COPYDATA && state && reinterpret_cast<HWND>(w)==state->server){
        const auto* data=reinterpret_cast<const COPYDATASTRUCT*>(l);
        if(data && data->dwData==reply_id && data->lpData && data->cbData<=1024*1024){
            state->reply.entries=parse_everything_reply({static_cast<const std::byte*>(data->lpData),data->cbData});
            state->reply.available=true;state->received=true;return TRUE;}}
    return DefWindowProcW(window,message,w,l);
}
}
EverythingReply query_everything(HWND server,std::wstring_view query,const std::function<bool()>& cancelled) {
    if(!server || query.empty() || query.size()>4096 || cancelled())return {};
    static const bool registered=[] {WNDCLASSW wc{};wc.lpfnWndProc=reply_proc;wc.hInstance=GetModuleHandleW(nullptr);
        wc.lpszClassName=L"FastPalette.EverythingReply";return RegisterClassW(&wc)!=0 || GetLastError()==ERROR_CLASS_ALREADY_EXISTS;}();
    if(!registered)return {};
    ReplyState state;state.server=server;
    const auto window=CreateWindowExW(0,L"FastPalette.EverythingReply",L"",0,0,0,0,0,HWND_MESSAGE,nullptr,GetModuleHandleW(nullptr),&state);
    if(!window)return {};
    std::vector<std::byte> packet(20+(query.size()+1)*sizeof(wchar_t));
    const DWORD fields[]={static_cast<DWORD>(reinterpret_cast<UINT_PTR>(window)),static_cast<DWORD>(reply_id),0,0,7};
    std::memcpy(packet.data(),fields,sizeof(fields));std::memcpy(packet.data()+20,query.data(),query.size()*sizeof(wchar_t));
    COPYDATASTRUCT request{2,static_cast<DWORD>(packet.size()),packet.data()};DWORD_PTR accepted=0;
    const auto sent=SendMessageTimeoutW(server,WM_COPYDATA,reinterpret_cast<WPARAM>(window),reinterpret_cast<LPARAM>(&request),SMTO_ABORTIFHUNG,100,&accepted);
    const auto deadline=GetTickCount64()+600;
    while(sent && accepted && !state.received && !cancelled() && GetTickCount64()<deadline){
        MSG message;while(PeekMessageW(&message,window,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
        if(!state.received)MsgWaitForMultipleObjectsEx(0,nullptr,20,QS_ALLINPUT,MWMO_INPUTAVAILABLE);}
    DestroyWindow(window);return std::move(state.reply);
}
}
