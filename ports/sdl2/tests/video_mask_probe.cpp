#include "dok/weapons.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
int main(int argc,char** argv){try{
    if(argc!=5)throw std::runtime_error("Usage: video_mask_probe RES WIDTH HEIGHT OUTPUT.mask");
    const auto archive=dok::Archive::open(std::filesystem::u8path(argv[1]));
    dok::World world(dok::load_level(archive,archive.identify()==dok::DatasetId::old_gold?"LEVEL4":"LEVEL1"));
    dok::Weapons weapons(world,archive);weapons.grant_all();
    dok::IndexedSurface overlay(std::stoi(argv[2]),std::stoi(argv[3]));weapons.overlay(overlay);
    std::ofstream out(std::filesystem::u8path(argv[4]),std::ios::binary);
    out.write(reinterpret_cast<const char*>(overlay.pixels.data()),overlay.pixels.size());
    if(!out)throw std::runtime_error("Cannot write weapon mask");
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
