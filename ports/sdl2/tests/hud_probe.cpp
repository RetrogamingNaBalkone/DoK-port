#include "dok/hud.hpp"
#include "dok/text.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
static void check(bool v){if(!v)throw std::runtime_error("HUD invariant failed");}
int main(int argc,char** argv){try{
    if(argc!=3)throw std::invalid_argument("Usage: dok_hud_probe RES OUTPUT.idx");
    const auto a=dok::Archive::open(std::filesystem::u8path(argv[1]));dok::Hud hud(a);unsigned cases=0;
    for(auto level:{"LEVEL1","LEVEL2","LEVEL3"}){
        for(auto prefix:{"BINTRO","EINTRO"}){
            const auto pages=dok::paginate_story(a.get(std::string(prefix)+level[5]));check(!pages.empty());
            for(const auto& page:pages){check(page.size()<=18);for(const auto& line:page)check(line.size()<=38&&line.find('$')==std::string::npos);}
        }
        dok::World world(dok::load_level(a,level));auto p=*world.objects.at(*world.player).player_state;
        dok::IndexedSurface scene(320,160);scene.clear(87);
        const auto baseline=hud.classic(scene,p);
        check(hud.classic(scene,p,"MESSAGE").pixels!=baseline.pixels);
        check(std::equal(scene.pixels.begin(),scene.pixels.end(),baseline.pixels.begin()));
        if(std::string(level)=="LEVEL1"){std::ofstream f(std::filesystem::u8path(argv[2]),std::ios::binary);f.write(reinterpret_cast<const char*>(baseline.pixels.data()),baseline.pixels.size());check(bool(f));}
        for(int mask=0;mask<8;++mask)for(int value:{0,1,9,100,32767}){
            p.attributes[0]=p.attributes[1]=static_cast<std::int16_t>(value);p.attributes[2]=p.attributes[3]=32767;
            p.attributes[10]=static_cast<std::int16_t>(mask);p.weapons[1]=1;p.weapons[2]=2;p.current_weapon=1;
            auto classic=hud.classic(scene,p);check(classic.pixels.size()==64000);
            for(auto size:{std::pair<int,int>{160,160},{320,160},{320,192},{640,360},{640,400},{640,384},{320,320}}){
                dok::IndexedSurface overlay(size.first,size.second);overlay.clear(87);const auto plates=hud.overlay(overlay,p,std::string(38,'M'));
                check(plates.size()==2);
                for(const auto& plate:plates)check(plate.x>=0&&plate.y>=0&&plate.x+plate.width<=size.first&&plate.y+plate.height<=size.second);
                check(plates[1].y+plates[1].height<plates[0].y);
                check(overlay.pixels[overlay.width()*20+overlay.width()/2]==87);
                ++cases;
            }
        }
        check(hud.classic(scene,*world.objects.at(*world.player).player_state).pixels==baseline.pixels);
    }
    std::cout<<cases<<" overlay states; classic reset and scene preservation passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
