#include "dok/geometry.hpp"
#include "dok/arithmetic.hpp"
#include "dok/scene.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

int main(int argc,char** argv){
    try{
        if(argc<3||argc>5)throw std::invalid_argument("Usage: dok_render_probe RES OUTPUT.idx [LEVEL [HEADING]]; omit level for door evidence");
        auto archive=dok::Archive::open(std::filesystem::u8path(argv[1]));
        if(archive.identify()!=dok::DatasetId::kremlin_pkr&&archive.identify()!=dok::DatasetId::kremlin_pke)
            throw std::invalid_argument("Door reference camera is verified only for PKR/PKE");
        dok::World world(dok::load_level(archive,argc>=4?argv[3]:"LEVEL1"));
        if(argc>=4){
            dok::SoftwareScene scene(archive,world.definition);
            auto pose=*world.objects.at(*world.player).pose;
            if(argc==5)pose.heading=static_cast<std::int16_t>(std::stoi(argv[4]));
            const auto& frame=scene.render(world,pose);
            std::ofstream file(std::filesystem::u8path(argv[2]),std::ios::binary);
            file.write(reinterpret_cast<const char*>(frame.pixels.data()),frame.pixels.size());
            if(!file)throw std::runtime_error("Cannot write scene frame");
            std::cout<<"Initial scene rendered: "<<scene.polygon_count()<<" polygons\n";
            return 0;
        }
        const auto& definition=*world.definition.object('d');
        auto image=archive.image(definition.sprite);image.flags|=definition.sprite_flags;
        auto light=archive.light(world.definition.light);
        dok::SoftwareCamera camera(dok::CameraParameters::kremlin_vga());
        camera.set_pose(*world.objects.at(*world.player).pose);
        dok::IndexedSurface surface(320,160);
        std::vector<dok::RenderPolygon> polygons;
        for(int row:{16,17}){
            const int z=(world.definition.length-1-row)*world.definition.cell_side;
            const auto width=dok::original::signed16(static_cast<std::uint16_t>(image.width*definition.width_parameter));
            const auto height=dok::original::signed16(static_cast<std::uint16_t>(image.height*definition.width_parameter));
            const int x=24*world.definition.cell_side+width/2;
            auto polygon=dok::vertical_plane(camera,{x,0,z,0},{x,0,z+width,0},height,
                image.width,image.height,static_cast<std::uint8_t>(definition.flags),width/4,0,0,row==17);
            if(!polygon)throw std::runtime_error("Expected reference door was rejected");
            polygons.push_back(std::move(*polygon));
        }
        dok::sort_and_cull(polygons,camera.clip());
        for(const auto& polygon:polygons)if(polygon.flags&1){
            image.flags=static_cast<std::uint16_t>((image.flags&~0x20)|(polygon.reverse_edges?0x20:0));
            dok::rasterize(surface,polygon.vertices,image,light,camera.clip(),dok::RasterAxis::vertical);
        }
        std::ofstream file(std::filesystem::u8path(argv[2]),std::ios::binary);
        file.write(reinterpret_cast<const char*>(surface.pixels.data()),surface.pixels.size());
        if(!file)throw std::runtime_error("Cannot write probe frame");
        std::cout<<"Rendered two original LEVEL1 door surfaces; full world not rendered\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
