#include "dok/filters.hpp"
#include <iostream>
#include <stdexcept>
#include <algorithm>
static void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
int main(){try{
    std::array<std::uint32_t,256> palette{};palette.fill(0xff000000);palette[9]=0xffffffff;palette[10]=0xffff0000;
    std::array<std::uint8_t,4096> lut{};
    for(int r=0;r<16;++r){lut[r*256+1]=r==0?9:10;lut[r*256+2]=0;}
    const dok::Bytes light{lut.data(),lut.size()};
    auto a=dok::make_mip_atlas({2,1,0,{1,2}},palette,light,false);
    check(a.pixels[2]==0xff808080,"Mipmaps must average composed RGB, not palette indices");
    check(a.pixels[a.tile_width+2]==0xff800000,"LIGHT rows must have independent pyramids");
    auto alpha=dok::make_mip_atlas({2,1,8,{1,0}},palette,light,true);
    check(alpha.pixels[2]==0x80808080,"Transparent mip must contain premultiplied white, not black fringe");
    check(dok::make_mip_atlas({1,1,8,{0}},palette,light,true).pixels[0]==0,"Transparent texel stays transparent");
    auto odd=dok::make_mip_atlas({3,1,0,{2,2,1}},palette,light,false);
    check((odd.pixels[3]&255)==85,"Odd-sized mipmaps must include last source column");
    auto tall=dok::make_mip_atlas({1,7,0,{1,1,1,1,1,1,1}},palette,light,false);
    check(tall.tile_width==3&&tall.pixels[2]==0xffffffff,"Rectangular pyramid reaches 1x1");
    const std::vector<std::uint32_t> solid(9,0xff123456);
    auto uniform=dok::xbr3(solid,3,3);check(uniform.size()==81&&std::all_of(uniform.begin(),uniform.end(),[](auto p){return p==0xff123456;}),"xBR must preserve flat colors");
    const std::vector<std::uint32_t> diagonal{0xffffffff,0xff000000,0xff000000,0xffffffff,0xffffffff,0xff000000,0xffffffff,0xffffffff,0xffffffff};
    const auto scaled=dok::xbr3(diagonal,3,3);bool intermediate=false;for(auto p:scaled)intermediate|=(p&255)>0&&(p&255)<255;
    check(intermediate,"xBR must reconstruct diagonal edges");
    const auto transparent=dok::xbr3(std::vector<std::uint32_t>(9,0),3,3);check(std::all_of(transparent.begin(),transparent.end(),[](auto p){return p==0;}),"xBR transparent background");
    std::cout<<"Lit RGB/alpha/odd/rectangular mipmaps and xBR contour/flat/alpha passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
