#include "providers.hpp"
#include "core.hpp"
#include <cstring>
#include <iostream>
using namespace palette;
int main(int argc,char**) {
    int failed=0;
    const auto check=[&](bool ok,const char* name){if(!ok){++failed;std::cerr<<"FAIL: "<<name<<'\n';}};
    const auto& pages=windows_settings();
    Settings routing;routing.search_everything=true;routing.everything_prefix_only=true;routing.everything_prefix=L"ef";
    check(!route_everything(L"report",routing).enabled,"prefix-only mode does not query Everything for ordinary searches");
    check(!route_everything(L"effect",routing).exclusive,"prefix must end at a token boundary");
    const auto routed=route_everything(L"ef ext:pdf <work|personal> !old size:>1mb",routing);
    check(routed.enabled && routed.exclusive && routed.text==L"ext:pdf <work|personal> !old size:>1mb","Everything syntax passes through unchanged after custom prefix");
    check(route_everything(L"ef",routing).exclusive && !route_everything(L"ef",routing).enabled,"bare prefix does not query the entire index");
    routing.everything_prefix_only=false;
    check(route_everything(L"regex:^report.*\\.pdf$",routing).text==L"regex:^report.*\\.pdf$","unprefixed syntax passes through in mixed mode");
    routing.search_everything=false;check(!route_everything(L"ef report",routing).exclusive,"disabled Everything does not claim prefix");
    for(const auto* query:{L"bluetooth",L"display",L"sound",L"windows update",L"default apps"})
        check(!search(pages,query,7).empty(),"common Windows Settings page is searchable");
    for(const auto& page:pages)check(page.target.starts_with(L"ms-settings:"),"settings use approved Windows URI scheme");
    SetEnvironmentVariableW(L"FAST_PALETTE_TEST_PATH",L"C:\\Windows");
    check(expand_path_query(L"%FAST_PALETTE_TEST_PATH%\\System32")==L"C:\\Windows\\System32","environment variable and suffix expansion");
    check(expand_path_query(L"  \"C:\\Program Files\"  ")==L"C:\\Program Files","quoted absolute path");
    check(expand_path_query(L"notepad").empty(),"ordinary searches are not paths");
    check(expand_path_query(L"%FAST_PALETTE_MISSING_VARIABLE%").empty(),"undefined variable does not produce a result");
    check(expand_path_query(L"ms-settings:display").empty(),"paths do not interpret arbitrary URI schemes");
    check(expand_path_query(L"C:relative").empty(),"drive-relative input does not depend on working directory");
    std::vector<std::byte> packet(40);
    const auto put=[&](size_t offset,DWORD value){std::memcpy(packet.data()+offset,&value,4);};
    put(20,1);put(32,40);
    const auto append=[&](const wchar_t* value){const auto length=(wcslen(value)+1)*sizeof(wchar_t);const auto start=packet.size();packet.resize(start+length);std::memcpy(packet.data()+start,value,length);};
    append(L"report.txt");put(36,static_cast<DWORD>(packet.size()));append(L"C:\\Users\\Example");
    const auto results=parse_everything_reply(packet);
    check(results.size()==1 && results[0].target==L"C:\\Users\\Example\\report.txt","Everything Unicode path decoding");
    check(parse_everything_reply(std::span(packet).first(27)).empty(),"truncated header rejected");
    put(32,0xffffffff);check(parse_everything_reply(packet).empty(),"out-of-range string offset rejected");
    put(32,41);check(parse_everything_reply(packet).empty(),"unaligned string rejected");
    put(20,0xffffffff);check(parse_everything_reply(packet).empty(),"oversized result count rejected");
    const auto missing=query_everything(nullptr,L"report",[]{return false;});
    check(!missing.available && missing.entries.empty(),"missing Everything returns immediately");
    WNDCLASSW fake_class{};fake_class.lpszClassName=L"FastPalette.Tests.SilentEverything";fake_class.hInstance=GetModuleHandleW(nullptr);
    fake_class.lpfnWndProc=[](HWND w,UINT m,WPARAM wp,LPARAM lp)->LRESULT{return m==WM_COPYDATA?TRUE:DefWindowProcW(w,m,wp,lp);};
    RegisterClassW(&fake_class);
    const auto silent=CreateWindowExW(0,fake_class.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,fake_class.hInstance,nullptr);
    check(silent!=nullptr,"silent IPC test server created");
    const auto timeout_start=GetTickCount64();
    const auto timeout=query_everything(silent,L"test",[]{return false;});
    check(!timeout.available && GetTickCount64()-timeout_start<1500,"unresponsive index has bounded wait");
    const auto cancel_start=GetTickCount64();
    const auto cancelled=query_everything(silent,L"test",[&]{return GetTickCount64()-cancel_start>=40;});
    check(!cancelled.available && GetTickCount64()-cancel_start<300,"superseded search cancels promptly");
    DestroyWindow(silent);UnregisterClassW(fake_class.lpszClassName,fake_class.hInstance);
    if(argc>1){const auto start=GetTickCount64();const auto live=query_everything(everything_window(),L"Everything.exe",[]{return false;});
        check(live.available && !live.entries.empty(),"live Everything returns indexed files");
        std::cout<<"Everything: "<<live.entries.size()<<" results in "<<GetTickCount64()-start<<" ms\n";
        const auto syntax=query_everything(everything_window(),L"file: ext:exe regex:^Everything\\.exe$",[]{return false;});
        check(syntax.available && !syntax.entries.empty(),"Everything native functions and regex work together");
        for(const auto& entry:syntax.entries)check(fold(entry.name)==L"everything.exe","Everything filters execute in its native engine");}
    SetEnvironmentVariableW(L"FAST_PALETTE_TEST_PATH",nullptr);
    return failed?1:0;
}
