#include "dok/text.hpp"
#include <algorithm>
#include <stdexcept>
namespace dok {
void menu_yo16(std::array<std::uint8_t,4096>& font){
    for(const auto pair:{std::pair<int,int>{0x85,0xf0},{0xa5,0xf1}}){
        std::array<std::uint8_t,16> body{};
        std::copy_n(font.begin()+pair.first*16,16,body.begin());
        int first=0,last=15;
        while(first<16&&!body[first])++first;
        while(last>=first&&!body[last])--last;
        if(first==16)continue;
        const int top=std::max(3,first),bottom=std::min(15,top+last-first);
        auto dest=font.begin()+pair.second*16;std::fill_n(dest,16,0);
        for(int row=top;row<=bottom;++row)
            dest[row]=body[first+(row-top)*(last-first+1)/(bottom-top+1)];
        // Two separated dots, then a clear row before the letter body.
        dest[top-3]=dest[top-2]=0x6c;
    }
}
std::string cp866(const std::string& text){
    std::string out;
    for(std::size_t i=0;i<text.size();){
        unsigned c=static_cast<unsigned char>(text[i++]);
        if(c>=0xc0){const int count=c<0xe0?1:c<0xf0?2:3;c&=count==1?31:count==2?15:7;
            for(int n=0;n<count&&i<text.size();++n)c=(c<<6)|(static_cast<unsigned char>(text[i++])&63);}
        if(c<128)out+=char(c);else if(c>=0x410&&c<=0x42f)out+=char(c-0x410+0x80);
        else if(c>=0x430&&c<=0x43f)out+=char(c-0x430+0xa0);else if(c>=0x440&&c<=0x44f)out+=char(c-0x440+0xe0);
        else if(c==0x401||c==0x451)out+=char(c==0x401?0xf0:0xf1);
        else if(c==0x2014||c==0x2013)out+='-';else if(c==0xab||c==0xbb)out+='"';else if(c==0xd7)out+='x';
        else if(c==0xa0)out+=' ';else if(c==0x2026)out+="...";else if(c==0x2019||c==0x2018)out+='\'';else out+='?';
    }return out;
}
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
