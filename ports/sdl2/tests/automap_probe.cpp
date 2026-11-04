#include "dok/automap.hpp"
#include "dok/png.hpp"
#include <fstream>
#include <iostream>
int main(int argc,char** argv){try{
    if(argc!=4)return 2;const auto archive=dok::Archive::open(std::filesystem::u8path(argv[1]));
    dok::World world(dok::load_level(archive,argv[2]));const auto& d=world.definition;const auto palette=archive.palette(d.palette);
    for(int mode=0;mode<3;++mode)for(int reveal=0;reveal<2;++reveal){
        world.map_revealed=reveal;std::fill(world.visited.begin(),world.visited.end(),mode==0?1:0);
        const auto& hero=world.objects.at(*world.player);const int hx=hero.pose->x/d.cell_side,hz=hero.pose->z/d.cell_side;
        if(mode==2)for(int z=std::max(0,hz-5);z<=std::min(d.length-1,hz+5);++z)for(int x=std::max(0,hx-5);x<=std::min(d.width-1,hx+5);++x)world.visited[z*d.width+x]=1;
        const auto map=dok::native_automap(world);const auto prefix=std::string(argv[3])+"-"+std::to_string(mode)+"-"+std::to_string(reveal);
        std::ofstream idx(prefix+".idx",std::ios::binary);idx.write(reinterpret_cast<const char*>(map.pixels.data()),map.pixels.size());
        std::vector<std::uint32_t> rgba;for(auto c:map.pixels)rgba.push_back(palette[c]);dok::save_png(std::filesystem::u8path(prefix+".png"),320,200,rgba);
        std::ofstream fixture(prefix+".fixture",std::ios::binary);auto word=[&](int n){fixture.put(n&255);fixture.put((n>>8)&255);};
        for(int n:{d.width,d.length,int(d.objects.size()),int(d.objects[hero.definition_index].glyph),reveal,hx,hz,int(hero.pose->heading)})word(n);
        for(const auto& o:d.objects)for(int n:{int(o.glyph),int(o.class_id),int(o.marker),0})word(n);
        for(const auto* plane:{"mmap","tmap","fcmap"}){const auto& data=world.maps.at(plane);fixture.write(reinterpret_cast<const char*>(data.data()),data.size());}
        fixture.write(reinterpret_cast<const char*>(world.visited.data()),world.visited.size());
    }
    std::cout<<"6 native map fixtures rendered\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
