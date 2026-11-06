#include "dok/png.hpp"
#include <filesystem>
#include <string>

int main(int argc,char** argv){
    if(argc!=2)return 2;
    const std::filesystem::path out=argv[1];
    // Cross DEFLATE length, block and 32 KiB dictionary boundaries; include
    // all literal values, overlap copies, hash collisions and random data.
    for(int kind=0;kind<4;++kind){
        const int width=kind==0?1:641,height=kind==0?1:257;
        std::vector<std::uint32_t> pixels;
        std::uint32_t random=123456789;
        for(int y=0;y<height;++y)for(int x=0;x<width;++x){
            random^=random<<13;random^=random>>17;random^=random<<5;
            pixels.push_back(kind==0?0xff123456:kind==1?0xff112233:
                             kind==2?0xff000000u|((x*17+y%7)&255)*0x010101u:random);
        }
        dok::save_png(out/(std::to_string(kind)+".png"),width,height,pixels);
    }
    try{dok::save_png(out/"invalid.png",0,1,{});return 3;}
    catch(const std::invalid_argument&){}
    return 0;
}
