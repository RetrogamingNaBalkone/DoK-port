#include "dok/hud.hpp"
#include "dok/png.hpp"
#include "dok/text.hpp"
#include "dok/scene.hpp"
#include <filesystem>
#include <algorithm>
#include <iostream>
#include <stdexcept>
int main(int argc,char** argv){try{
    if(argc!=3)throw std::runtime_error("RES output.png");
    const auto archive=dok::Archive::open(std::filesystem::u8path(argv[1]));dok::Hud hud(archive);
    const bool og=archive.identify()==dok::DatasetId::old_gold;
    dok::World world(dok::load_level(archive,og?"LEVEL4":"LEVEL1"));
    const auto player=*world.objects.at(*world.player).player_state;
    dok::SoftwareScene renderer(archive,world.definition);
    const auto native_pixels=renderer.render(world,*world.objects.at(*world.player).pose).pixels;
    renderer.software_presentation(320,og?170:160,og?170:160);
    if(renderer.render(world,*world.objects.at(*world.player).pose).pixels!=native_pixels)
        throw std::runtime_error("Classic VGA projection differs from original camera path");
    const dok::IndexedSurface scene(320,og?170:160);
    const auto blank=hud.classic(scene,player);
    for(const auto& message:{std::string("CENTER"),dok::cp866("ВСЕМ ВАМ, КРОВОСОСАМ, КРЫШКА!"),std::string(38,'M')}){
        const auto frame=hud.classic(scene,player,message);
        int left=320,right=-1,top=200,bottom=-1;
        for(int y=og?172:163;y<(og?180:174);++y)for(int x=4;x<316;++x){
            const auto i=y*320+x;
            if(frame.pixels[i]!=blank.pixels[i]){left=std::min(left,x);right=std::max(right,x);top=std::min(top,y);bottom=std::max(bottom,y);}
        }
        const auto font=archive.font("FONT8");int first_ink=320;
        for(std::size_t i=0;i<message.size();++i)for(int y=0;y<8;++y)for(int x=0;x<8;++x)
            if(font.data[static_cast<unsigned char>(message[i])*8+y]&(0x80>>x))first_ink=std::min(first_ink,int(i)*8+x);
        if(right<left||left!=4+first_ink||std::abs(top+bottom-(og?351:336))>1)
            throw std::runtime_error("Message must preserve DOS x=4 and center vertically");
        if(message.size()!=38){
            const auto palette=archive.palette(world.definition.palette);
            std::vector<std::uint32_t> pixels(frame.pixels.size());
            std::transform(frame.pixels.begin(),frame.pixels.end(),pixels.begin(),[&](auto c){return palette[c];});
            dok::save_png(std::filesystem::u8path(argv[2]),320,200,pixels);
        }
    }
    std::cout<<"Classic VGA exact; DOS left origin and vertical centering: ASCII, Russian and 38 characters passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
