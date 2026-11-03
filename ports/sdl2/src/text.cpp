#include "dok/text.hpp"
#include <algorithm>
#include <stdexcept>
namespace dok {
std::vector<TextPage> paginate_story(Bytes bytes,unsigned columns,unsigned rows){
    if(!columns||!rows)throw std::invalid_argument("Empty text page dimensions");
    std::string source(reinterpret_cast<const char*>(bytes.data),bytes.size);
    const auto end=source.find_first_of("$\0",0,2);if(end!=std::string::npos)source.resize(end);
    std::vector<std::string> lines;
    for(std::size_t pos=0;pos<source.size();){
        auto endline=source.find_first_of("\r\n",pos);if(endline==std::string::npos)endline=source.size();
        auto line=source.substr(pos,endline-pos);pos=endline;
        if(pos<source.size()&&source[pos++]=='\r'&&pos<source.size()&&source[pos]=='\n')++pos;
        while(line.size()>columns){
            auto split=line.rfind(' ',columns);if(split==std::string::npos||split==0)split=columns;
            lines.push_back(line.substr(0,split));line.erase(0,split);
            if(!line.empty()&&line.front()==' ')line.erase(0,1);
        }
        lines.push_back(line);
    }
    while(!lines.empty()&&lines.back().empty())lines.pop_back();
    std::vector<TextPage> pages;
    for(std::size_t i=0;i<lines.size();i+=rows)pages.emplace_back(lines.begin()+i,lines.begin()+std::min(lines.size(),i+rows));
    return pages;
}
}
