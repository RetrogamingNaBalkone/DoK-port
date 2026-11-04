#include "dok/scene.hpp"
#include "dok/arithmetic.hpp"
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>

// DOKR1 is the original RAM/image capture format used by render_probe.
static void native_state(const std::string& path,dok::World& world,std::map<dok::OwnerId,dok::Image>& images){
    std::ifstream in(path,std::ios::binary);char magic[5];in.read(magic,5);
    if(std::string(magic,5)!="DOKR1")throw std::runtime_error("Wrong native state header");
    auto u=[&](int n){std::uint32_t v=0;for(int j=0;j<n;++j){const auto c=in.get();if(c<0)throw std::runtime_error("Truncated native state");v|=std::uint32_t(c)<<(8*j);}return v;};
    const auto count=u(2);if(count>256)throw std::runtime_error("Native state count");
    for(unsigned i=0;i<count;++i){
        const auto glyph=u(1),cls=u(2),flags=u(2),has_pose=u(1);dok::Pose p;
        p.x=dok::original::signed32(u(4));p.y=dok::original::signed32(u(4));p.z=dok::original::signed32(u(4));p.heading=dok::original::signed16(static_cast<std::uint16_t>(u(2)));
        const auto phase=dok::original::signed16(static_cast<std::uint16_t>(u(2))),extent=dok::original::signed16(static_cast<std::uint16_t>(u(2)));
        dok::Image image;image.width=u(2);image.height=u(2);image.flags=u(2);const auto size=u(4);
        if(size!=std::size_t(image.width)*image.height||size>4194304)throw std::runtime_error("Native image size");
        image.pixels.resize(size);in.read(reinterpret_cast<char*>(image.pixels.data()),size);if(!in)throw std::runtime_error("Truncated native image");
        auto& o=world.objects.at(world.definition.glyph_index.at(glyph));
        if(world.definition.objects[o.definition_index].class_id!=cls)throw std::runtime_error("Native glyph/class mismatch");
        o.flags=static_cast<std::uint16_t>(flags);if(has_pose)o.pose=p;if(o.door){o.door->phase=phase;o.door->extent=extent;}images.emplace(o.owner,std::move(image));
    }
}
int main(int argc,char** argv){
    try{
        if(argc<4)throw std::runtime_error("Usage: dok_map_discovery_probe RES LEVEL HEADING [--native-state FILE] [--pose X Y Z H]");
        const auto archive=dok::Archive::open(std::filesystem::u8path(argv[1]));
        dok::World world(dok::load_level(archive,argv[2]));
        auto pose=*world.objects.at(*world.player).pose;pose.heading=std::stoi(argv[3]);
        std::map<dok::OwnerId,dok::Image> images;
        for(int i=4;i<argc;++i){const std::string option=argv[i];
            if(option=="--native-state"&&i+1<argc)native_state(argv[++i],world,images);
            else if(option=="--pose"&&i+4<argc){pose.x=std::stoi(argv[++i]);pose.y=std::stoi(argv[++i]);pose.z=std::stoi(argv[++i]);pose.heading=std::stoi(argv[++i]);}
            else throw std::runtime_error("Unknown probe option");
        }
        const auto getter=[&](auto owner)->const dok::Image*{const auto found=images.find(owner);return found==images.end()?nullptr:&found->second;};
        const auto maps=world.maps;const auto visited=world.visited;const auto ticks=world.simulation_ticks();
        dok::SoftwareScene scene(archive,world.definition);
        const auto before=scene.render(world,pose,{}, {},320,{},getter).pixels;
        const auto cells=scene.discovered_cells(world,pose,{},getter);
        if(before!=scene.render(world,pose,{}, {},320,{},getter).pixels)throw std::runtime_error("Discovery changed scene pixels");
        scene.software_presentation(1200,540);scene.base_fov(110);
        if(cells!=scene.discovered_cells(world,pose,{},getter))throw std::runtime_error("Presentation changed discovery");
        if(world.maps!=maps||world.visited!=visited||world.simulation_ticks()!=ticks)
            throw std::runtime_error("Discovery mutated the world");
        std::cout<<"{\"pose\":["<<pose.x<<','<<pose.y<<','<<pose.z<<','<<pose.heading<<"],\"cells\":[";
        for(std::size_t i=0;i<cells.size();++i){if(i)std::cout<<',';std::cout<<cells[i];}
        std::cout<<"]}\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
