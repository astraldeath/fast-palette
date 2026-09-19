#include "modules.hpp"
#include <iostream>
using namespace palette;
int main(){int failed=0;const auto check=[&](bool value,const char* name){if(!value){++failed;std::cerr<<"FAIL: "<<name<<'\n';}};
    Settings s;s.search_everything=true;
    auto route=route_query(L"win bluetooth",s);
    check(route.exclusive==Module::settings && route.text==L"bluetooth" && !route.allows(Module::everything),"prefix isolates Windows Settings");
    route=route_query(L"=2pi",s);check(route.exclusive==Module::calculator && route.text==L"2pi","calculator prefix without separator");
    set_module_rule(s,Module::apps,{L"apps",true});
    check(!route_query(L"notepad",s).allows(Module::apps),"prefix-only module excluded from ordinary search");
    check(route_query(L"apps notepad",s).allows(Module::apps),"prefix-only module responds to its prefix");
    check(!route_query(L"appsnotepad",s).exclusive,"prefix requires word boundary");
    s.search_apps=false;check(!route_query(L"apps notepad",s).allows(Module::apps),"disabled module stays disabled with prefix");
    route=route_query(L"?  regex:^a.*$ | ext:txt",s);
    check(route.text==L" regex:^a.*$ | ext:txt" && route.exclusive==Module::everything,"Everything syntax retained after separator");
    route=route_query(L"fx usd to yen",s);
    check(route.exclusive==Module::currency && route.text==L"usd to yen" && !route.allows(Module::conversions),"currency prefix isolates conversions");
    set_module_rule(s,Module::currency,{L"fx",true});
    check(!route_query(L"usd to won",s).allows(Module::currency),"currency prefix-only excludes unprefixed queries");
    s.search_currency=false;
    check(!route_query(L"fx usd to yen",s).allows(Module::currency),"disabled currency prefix stays disabled");
    std::wstring error;set_module_rule(s,Module::paths,{L"win",false});check(!valid_module_settings(s,error),"duplicate prefixes rejected");
    set_module_rule(s,Module::paths,{L"",false});check(valid_module_settings(s,error),"optional prefix can be cleared");
    set_module_rule(s,Module::paths,{L"",true});check(!valid_module_settings(s,error),"prefix-only needs prefix");
    s=Settings{};s.aliases={{L"work",L"%appdata%",L""},{L"edit",L"notepad.exe",L"example.txt"}};
    const auto aliases=alias_entries(s);check(aliases.size()==2 && aliases[1].arguments==L"example.txt" && aliases[0].source==SearchSource::alias,"alias targets and arguments preserved");
    s.aliases.push_back({L"WORK",L"C:\\",L""});check(!valid_module_settings(s,error),"alias names unique regardless of case");
    return failed?1:0;
}
