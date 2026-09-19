#include "modules.hpp"
#include "core.hpp"
#include <algorithm>
#include <cwctype>
namespace palette {
PrefixRule module_rule(const Settings& s,Module module) {
    if(module==Module::everything)return {s.everything_prefix,s.everything_prefix_only};
    return s.prefixes[static_cast<size_t>(module)];
}
void set_module_rule(Settings& s,Module module,const PrefixRule& rule) {
    if(module==Module::everything){s.everything_prefix=rule.prefix;s.everything_prefix_only=rule.only;}
    else s.prefixes[static_cast<size_t>(module)]=rule;
}
bool module_enabled(const Settings& s,Module module) {
    switch(module){
    case Module::apps:return s.search_apps;
    case Module::calculator:return s.search_calculator;
    case Module::settings:return s.search_settings;
    case Module::paths:return s.search_paths;
    case Module::everything:return s.search_everything;
    case Module::conversions:return s.search_conversions;
    case Module::aliases:return s.search_aliases;
    case Module::currency:return s.search_currency;
    default:return false;
    }
}
bool valid_module_settings(const Settings& s,std::wstring& error) {
    for(size_t i=0;i<module_count;++i){
        const auto rule=module_rule(s,static_cast<Module>(i));
        if(rule.prefix.size()>16 || (rule.prefix.empty() && rule.only) ||
           std::any_of(rule.prefix.begin(),rule.prefix.end(),[](wchar_t c){return iswspace(c)||iswcntrl(c);}) ||
           (i!=static_cast<size_t>(Module::calculator) && rule.prefix.starts_with(L"="))){
            error=L"Prefixes must be unique, up to 16 characters, without spaces. Only Calculator can use =.";return false;
        }
        if(rule.prefix.empty())continue;
        for(size_t j=0;j<i;++j)if(fold(rule.prefix)==fold(module_rule(s,static_cast<Module>(j)).prefix)){
            error=L"Each module needs a different prefix.";return false;
        }
    }
    if(s.aliases.size()>128){error=L"Up to 128 aliases are supported.";return false;}
    size_t serialized_aliases=sizeof(wchar_t);
    for(size_t i=0;i<s.aliases.size();++i){const auto& alias=s.aliases[i];
        serialized_aliases+=(alias.name.size()+alias.target.size()+alias.arguments.size()+4)*sizeof(wchar_t);
        if(serialized_aliases>4*1024*1024){error=L"Alias entries are too large.";return false;}
        if(alias.name.empty() || alias.name.size()>64 || alias.target.empty() || alias.target.size()>32767 || alias.arguments.size()>32767 ||
           std::any_of(alias.name.begin(),alias.name.end(),[](wchar_t c){return iswcntrl(c);}) ||
           alias.target.find(L'\0')!=std::wstring::npos || alias.arguments.find(L'\0')!=std::wstring::npos){error=L"Each alias needs a name and a target.";return false;}
        for(size_t j=0;j<i;++j)if(fold(alias.name)==fold(s.aliases[j].name)){error=L"Alias names must be unique.";return false;}
    }
    return true;
}
RoutedQuery route_query(std::wstring_view query,const Settings& s) {
    RoutedQuery result;const auto first=query.find_first_not_of(L" \t\r\n");
    const auto input=first==std::wstring_view::npos?std::wstring_view{}:query.substr(first);
    for(size_t i=0;i<module_count;++i){const auto module=static_cast<Module>(i);const auto rule=module_rule(s,module);
        result.enabled[i]=module_enabled(s,module) && !rule.only;
        if(!module_enabled(s,module) || rule.prefix.empty() || input.size()<rule.prefix.size())continue;
        if(fold(input.substr(0,rule.prefix.size()))!=fold(rule.prefix))continue;
        if(input.size()!=rule.prefix.size() && !(module==Module::calculator && rule.prefix==L"=") && !iswspace(input[rule.prefix.size()]))continue;
        result.exclusive=module;result.text=std::wstring(input.substr(rule.prefix.size()));
        if(!result.text.empty() && iswspace(result.text.front()))result.text.erase(0,1);
    }
    if(result.exclusive){result.enabled.fill(false);result.enabled[static_cast<size_t>(*result.exclusive)]=true;}
    else result.text=std::wstring(query);
    return result;
}
std::vector<AppEntry> alias_entries(const Settings& s) {
    std::vector<AppEntry> result;result.reserve(s.aliases.size());
    for(const auto& alias:s.aliases){AppEntry entry;entry.name=alias.name;entry.target=alias.target;entry.arguments=alias.arguments;
        entry.id=L"alias:"+fold(alias.name)+L"|"+alias.target+L"|"+alias.arguments;entry.detail=alias.target;entry.folded=fold(alias.name);entry.kind=LaunchKind::executable;entry.source=SearchSource::alias;
        result.push_back(std::move(entry));}
    return result;
}
}
