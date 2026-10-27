#pragma once
#include <string>
#include <vector>
namespace phthongos::detail {
inline std::string trim(std::string s) {
    // Rust str::trim uses Unicode White_Space. Keep user-created voice names
    // identical, including NBSP and full-width spaces, without a GUI dependency.
    static const std::vector<std::string> spaces={
        " ","\t","\r","\n","\v","\f",u8"\u0085",u8"\u00a0",u8"\u1680",
        u8"\u2000",u8"\u2001",u8"\u2002",u8"\u2003",u8"\u2004",u8"\u2005",
        u8"\u2006",u8"\u2007",u8"\u2008",u8"\u2009",u8"\u200a",
        u8"\u2028",u8"\u2029",u8"\u202f",u8"\u205f",u8"\u3000"};
    bool changed=true;
    while(changed){changed=false;for(const auto& token:spaces)
        if(s.compare(0,token.size(),token)==0){s.erase(0,token.size());changed=true;break;}}
    changed=true;
    while(changed){changed=false;for(const auto& token:spaces)
        if(s.size()>=token.size()&&s.compare(s.size()-token.size(),token.size(),token)==0){s.resize(s.size()-token.size());changed=true;break;}}
    return s;
}
}
