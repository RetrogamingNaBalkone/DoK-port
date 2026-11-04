#include "dok/geometry.hpp"
#include "dok/arithmetic.hpp"
#include "dok/scene.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <map>

static void native_state(const std::string& path,dok::World& world,std::map<dok::OwnerId,dok::Image>& images){
    std::ifstream in(path,std::ios::binary);char magic[5];in.read(magic,5);if(std::string(magic,5)!="DOKR1")throw std::runtime_error("Wrong native state header");
    auto u=[&](int n){std::uint32_t v=0;for(int j=0;j<n;++j){const auto c=in.get();if(c<0)throw std::runtime_error("Truncated native state");v|=std::uint32_t(c)<<(8*j);}return v;};
    const auto count=u(2);if(count>256)throw std::runtime_error("Native state count");
    for(unsigned i=0;i<count;++i){const auto glyph=u(1),cls=u(2),flags=u(2),has_pose=u(1);dok::Pose p;
        p.x=dok::original::signed32(u(4));p.y=dok::original::signed32(u(4));p.z=dok::original::signed32(u(4));p.heading=dok::original::signed16(static_cast<std::uint16_t>(u(2)));
        const auto phase=dok::original::signed16(static_cast<std::uint16_t>(u(2))),extent=dok::original::signed16(static_cast<std::uint16_t>(u(2)));
        dok::Image image;image.width=u(2);image.height=u(2);image.flags=u(2);const auto size=u(4);
        if(size!=std::size_t(image.width)*image.height||size>4194304)throw std::runtime_error("Native image size");image.pixels.resize(size);in.read(reinterpret_cast<char*>(image.pixels.data()),size);if(!in)throw std::runtime_error("Truncated native image");
        auto& o=world.objects.at(world.definition.glyph_index.at(glyph));if(world.definition.objects[o.definition_index].class_id!=cls)throw std::runtime_error("Native glyph/class mismatch");
        o.flags=static_cast<std::uint16_t>(flags);if(has_pose)o.pose=p;if(o.door){o.door->phase=phase;o.door->extent=extent;}images.emplace(o.owner,std::move(image));
    }
}

int main(int argc,char** argv){
    try{
        if(argc<3)throw std::invalid_argument("Usage: dok_render_probe RES OUTPUT.idx [LEVEL [HEADING|--pose X Y Z H] [--resolution W H] [--native-state FILE] [--native-light-history] [--geometry FILE]]; omit LEVEL for door evidence");
        auto archive=dok::Archive::open(std::filesystem::u8path(argv[1]));
        if(argc<4&&archive.identify()!=dok::DatasetId::kremlin_pkr&&archive.identify()!=dok::DatasetId::kremlin_pke)
            throw std::invalid_argument("Door reference camera is verified only for PKR/PKE");
        dok::World world(dok::load_level(archive,argc>=4?argv[3]:"LEVEL1"));
        if(argc>=4){
            dok::SoftwareScene scene(archive,world.definition);
            std::map<dok::OwnerId,dok::Image> snapshot_images;
            auto pose=*world.objects.at(*world.player).pose;
            for(int i=4;i<argc;++i){const std::string arg=argv[i];
                if(arg=="--native-light-history")scene.native_light_history(true);
                else if(arg=="--native-state"&&i+1<argc){native_state(argv[++i],world,snapshot_images);pose=*world.objects.at(*world.player).pose;}
                else if(arg=="--resolution"&&i+2<argc){const int w=std::stoi(argv[++i]),h=std::stoi(argv[++i]);scene.software_resolution(w,h);}
                else if(arg=="--pose"&&i+4<argc){pose.x=std::stoi(argv[++i]);pose.y=std::stoi(argv[++i]);pose.z=std::stoi(argv[++i]);pose.heading=static_cast<std::int16_t>(std::stoi(argv[++i]));}
                else if(arg=="--object-pose"&&i+5<argc){
                    const auto glyph=std::stoi(argv[++i]);
                    auto& object=world.objects.at(world.definition.glyph_index.at(glyph));
                    dok::Pose p;p.x=std::stoi(argv[++i]);p.y=std::stoi(argv[++i]);p.z=std::stoi(argv[++i]);p.heading=static_cast<std::int16_t>(std::stoi(argv[++i]));object.pose=p;
                }
                else if(arg=="--geometry"&&i+1<argc){const std::string path=argv[++i];scene.inspect_geometry([path](const auto& polygons){
                    std::ofstream out(path);for(const auto& p:polygons){out<<int(p.axis)<<" "<<p.material<<" "<<p.sort_key;for(const auto& v:p.vertices)out<<" "<<v.x<<","<<v.y<<","<<v.depth;out<<" |";for(const auto& v:p.secondary)out<<" "<<v.x<<","<<v.y<<","<<v.depth;out<<"\n";}
                });}
                else pose.heading=static_cast<std::int16_t>(std::stoi(arg));
            }
            const auto& frame=scene.render(world,pose,{}, {},320,{},[&](auto owner)->const dok::Image*{const auto it=snapshot_images.find(owner);return it==snapshot_images.end()?nullptr:&it->second;});
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
