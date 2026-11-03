#include "dok/png.hpp"
#include <algorithm>
#include <fstream>
#include <stdexcept>
namespace dok {
namespace {
void word(std::vector<std::uint8_t>& out,std::uint32_t n){
    for(int shift:{24,16,8,0})out.push_back(static_cast<std::uint8_t>(n>>shift));
}
void chunk(std::vector<std::uint8_t>& out,const char* type,const std::vector<std::uint8_t>& data){
    word(out,static_cast<std::uint32_t>(data.size()));const auto start=out.size();
    out.insert(out.end(),type,type+4);out.insert(out.end(),data.begin(),data.end());
    std::uint32_t crc=0xffffffffu;
    for(auto i=start;i<out.size();++i){crc^=out[i];for(int bit=0;bit<8;++bit)crc=(crc>>1)^((crc&1)?0xedb88320u:0);}
    word(out,crc^0xffffffffu);
}
}
void save_png(const std::filesystem::path& path,int width,int height,const std::vector<std::uint32_t>& pixels){
    if(width<=0||height<=0||pixels.size()!=std::size_t(width)*height)throw std::invalid_argument("Invalid PNG dimensions");
    std::vector<std::uint8_t> raw;raw.reserve((std::size_t(width)*3+1)*height);
    for(int y=0;y<height;++y){raw.push_back(0);for(int x=0;x<width;++x){const auto p=pixels[std::size_t(y)*width+x];
        raw.push_back(p>>16);raw.push_back(p>>8);raw.push_back(p);}}
    // Standard zlib stream with stored DEFLATE blocks: no external codec DLL.
    std::vector<std::uint8_t> compressed{0x78,0x01};
    std::uint32_t a=1,b=0;
    for(auto c:raw){a=(a+c)%65521;b=(b+a)%65521;}
    for(std::size_t pos=0;pos<raw.size();){
        const auto count=static_cast<std::uint16_t>(std::min<std::size_t>(65535,raw.size()-pos));
        compressed.push_back(pos+count==raw.size()?1:0);
        compressed.push_back(count);compressed.push_back(count>>8);
        compressed.push_back(static_cast<std::uint8_t>(~count));compressed.push_back(static_cast<std::uint8_t>((~count)>>8));
        compressed.insert(compressed.end(),raw.begin()+pos,raw.begin()+pos+count);pos+=count;
    }
    word(compressed,(b<<16)|a);
    std::vector<std::uint8_t> out{137,80,78,71,13,10,26,10},header;
    word(header,width);word(header,height);header.insert(header.end(),{8,2,0,0,0});
    chunk(out,"IHDR",header);chunk(out,"IDAT",compressed);chunk(out,"IEND",{});
    std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<const char*>(out.data()),static_cast<std::streamsize>(out.size()));
    file.close();if(!file)throw std::runtime_error("Cannot save PNG: "+path.u8string());
}
}
