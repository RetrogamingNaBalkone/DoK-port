#include "dok/png.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>
namespace dok {
namespace {
void word(std::vector<std::uint8_t>& out,std::uint32_t n){
    for(int shift:{24,16,8,0})out.push_back(static_cast<std::uint8_t>(n>>shift));
}
// Fixed-Huffman DEFLATE with a bounded LZ77 dictionary. No codec DLL is
// required, including in headless probes. Stored blocks made UI matrices
// consume tens of gigabytes even though their pixels are highly repetitive.
std::vector<std::uint8_t> deflate(const std::vector<std::uint8_t>& raw){
    std::vector<std::uint8_t> out{0x78,0x01};
    unsigned bits=0,used=0;
    auto put=[&](unsigned value,unsigned count){
        bits|=value<<used;used+=count;
        while(used>=8){out.push_back(static_cast<std::uint8_t>(bits));bits>>=8;used-=8;}
    };
    auto reversed=[](unsigned value,unsigned count){
        unsigned result=0;while(count--){result=(result<<1)|(value&1);value>>=1;}return result;
    };
    auto symbol=[&](unsigned value){
        unsigned code,count;
        if(value<144){code=0x30+value;count=8;}
        else if(value<256){code=0x190+value-144;count=9;}
        else if(value<280){code=value-256;count=7;}
        else{code=0xc0+value-280;count=8;}
        put(reversed(code,count),count);
    };
    constexpr unsigned length_base[]={3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
    constexpr unsigned length_extra[]={0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
    constexpr unsigned distance_base[]={1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
    constexpr unsigned distance_extra[]={0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
    std::array<std::size_t,65536> dictionary;
    dictionary.fill(raw.size());
    auto hash=[&](std::size_t pos){return ((unsigned(raw[pos])*251+raw[pos+1])*251+raw[pos+2])&65535;};
    put(3,3); // Final block, fixed Huffman codes.
    for(std::size_t pos=0;pos<raw.size();){
        std::size_t length=0,distance=0;
        if(pos+2<raw.size()){
            const auto previous=dictionary[hash(pos)];
            if(previous<pos&&pos-previous<=32768){
                const auto limit=std::min<std::size_t>(258,raw.size()-pos);
                while(length<limit&&raw[previous+length]==raw[pos+length])++length;
                distance=pos-previous;
            }
        }
        if(length<3){symbol(raw[pos]);length=1;}
        else{
            unsigned code=0;while(code<28&&length>=length_base[code+1])++code;
            symbol(257+code);put(unsigned(length)-length_base[code],length_extra[code]);
            code=0;while(code<29&&distance>=distance_base[code+1])++code;
            put(reversed(code,5),5);put(unsigned(distance)-distance_base[code],distance_extra[code]);
        }
        for(std::size_t end=pos+length;pos<end;++pos)
            if(pos+2<raw.size())dictionary[hash(pos)]=pos;
    }
    symbol(256);if(used)out.push_back(static_cast<std::uint8_t>(bits));
    std::uint32_t a=1,b=0;
    for(auto c:raw){a=(a+c)%65521;b=(b+a)%65521;}
    word(out,(b<<16)|a);return out;
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
    const auto compressed=deflate(raw);
    std::vector<std::uint8_t> out{137,80,78,71,13,10,26,10},header;
    word(header,width);word(header,height);header.insert(header.end(),{8,2,0,0,0});
    chunk(out,"IHDR",header);chunk(out,"IDAT",compressed);chunk(out,"IEND",{});
    std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<const char*>(out.data()),static_cast<std::streamsize>(out.size()));
    file.close();if(!file)throw std::runtime_error("Cannot save PNG: "+path.u8string());
}
}
