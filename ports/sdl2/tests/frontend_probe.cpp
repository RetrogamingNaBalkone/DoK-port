#include "dok/frontend.hpp"
#include "dok/display.hpp"
#include "dok/scene.hpp"
#include "dok/hud.hpp"
#include "dok/weapons.hpp"
#include "dok/png.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void key(dok::Frontend& menu,SDL_Keycode code){SDL_Event e{};e.type=SDL_KEYDOWN;e.key.keysym.sym=code;e.key.keysym.scancode=SDL_GetScancodeFromKey(code);menu.handle(e,640,480);menu.render(640,480);}
void shot(dok::Display& display,dok::Frontend& menu,const std::filesystem::path& path){display.begin();auto size=display.size();const auto& frame=menu.render(size[0],size[1]);display.pixels(frame.pixels,frame.width,frame.height,false);display.screenshot(path.u8string());display.present();}
std::uint64_t digest(const std::vector<std::uint8_t>& pixels){std::uint64_t h=1469598103934665603ULL;for(auto p:pixels){h^=p;h*=1099511628211ULL;}return h;}
}
int main(int argc,char** argv){
    SDL_SetMainReady();if(argc!=3||SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_GAMECONTROLLER)!=0)return 2;int result=0;
    try{
        const std::filesystem::path root=std::filesystem::u8path(argv[1]),out=std::filesystem::u8path(argv[2]);std::filesystem::create_directories(out);
        std::vector<dok::Archive> sets;for(const auto* path:{"originals/pkr/RUS/PKR.RES","originals/pkr/ENG/PKE.RES","originals/Old_Gold_3D/OLD_GOLD.RES"})sets.push_back(dok::Archive::open(root/path));
        unsigned combinations=0,frames=0,software=0;
        for(int mask=1;mask<8;++mask){dok::AppSettings s;dok::Frontend menu(s);std::vector<dok::FrontendDataset> values;
            for(int i=0;i<3;++i)if(mask&(1<<i))values.push_back({&sets[i],std::to_string(i)});const auto count=values.size();menu.datasets(values);menu.render(640,480);
            key(menu,SDLK_RETURN);check(menu.screen()==(count==1?dok::MenuScreen::title:dok::MenuScreen::datasets),"Dataset cardinality route");
            if(count>1)key(menu,SDLK_RETURN);check(menu.screen()==dok::MenuScreen::title,"Dataset title route");
            key(menu,SDLK_RETURN);const bool og=values[0].archive->identify()==dok::DatasetId::old_gold;
            check(menu.screen()==(og?dok::MenuScreen::levels:dok::MenuScreen::readme),"Original story sequence");
            for(int page=0;menu.screen()==dok::MenuScreen::readme&&page<100;++page)key(menu,SDLK_RETURN);
            check(menu.screen()==dok::MenuScreen::levels,"Story terminates");key(menu,SDLK_RETURN);check(!menu.active(),"Start from preview");++combinations;
        }
        for(int backend=0;backend<2;++backend)for(int lang=0;lang<2;++lang)for(auto size:{std::array<int,2>{640,480},std::array<int,2>{1280,720},std::array<int,2>{1920,1080}}){
            dok::AppSettings s;s.video.renderer=backend?dok::RendererKind::opengl:dok::RendererKind::software;s.video.width=size[0];s.video.height=size[1];s.locale=lang?dok::LocaleId::en:dok::LocaleId::ru;
            dok::Display display(s.video);dok::Frontend menu(s);menu.datasets({{&sets[0],"PKR.RES"},{&sets[1],"PKE.RES"},{&sets[2],"OLD_GOLD.RES"}});
            const auto prefix=(backend?"gl-":"sw-")+std::string(lang?"en-":"ru-")+std::to_string(size[0])+"-";
            shot(display,menu,out/(prefix+"main.png"));key(menu,SDLK_RETURN);shot(display,menu,out/(prefix+"datasets.png"));
            key(menu,SDLK_RETURN);shot(display,menu,out/(prefix+"title.png"));key(menu,SDLK_RETURN);
            for(int page=0;menu.screen()==dok::MenuScreen::readme&&page<100;++page)key(menu,SDLK_RETURN);
            shot(display,menu,out/(prefix+"levels.png"));key(menu,SDLK_ESCAPE);key(menu,SDLK_DOWN);key(menu,SDLK_RETURN);key(menu,SDLK_RETURN);shot(display,menu,out/(prefix+"graphics.png"));
            key(menu,SDLK_ESCAPE);key(menu,SDLK_ESCAPE);key(menu,SDLK_DOWN);key(menu,SDLK_RETURN);shot(display,menu,out/(prefix+"credits.png"));frames+=6;
        }
        std::ofstream modes(out/"software-modes.tsv");modes<<"dataset\tlevel\twidth\theight\tscene_height\tfnv64\n";
        for(int dataset=0;dataset<3;++dataset){const auto& a=sets[dataset];const int count=dataset==2?1:3;
            for(int level=1;level<=count;++level){const auto name=dataset==2?"LEVEL4":"LEVEL"+std::to_string(level);dok::World world(dok::load_level(a,name));dok::SoftwareScene scene(a,world.definition);dok::Hud hud(a);
                std::vector<std::uint8_t> vga;
                for(auto size:dok::software_modes){scene.software_resolution(size[0],size[1]);const auto& frame=scene.render(world,*world.objects.at(*world.player).pose);
                    check(frame.width()==size[0]&&frame.height()==(int(size[1]-39.0f*(float(size[1])/200))/2)*2,"Native viewport dimensions");
                    const auto composed=hud.classic_mode(frame,size[1],*world.objects.at(*world.player).player_state);
                    check(composed.width()==size[0]&&composed.height()==size[1],"Native framebuffer dimensions");
                    if(size[0]==320)vga=frame.pixels;
                    else{std::size_t differences=0;for(int y=0;y<frame.height();++y)for(int x=0;x<frame.width();++x)differences+=frame.pixels[y*frame.width()+x]!=vga[(y*160/frame.height())*320+x*320/frame.width()];
                        check(differences>frame.pixels.size()/100,"High-resolution world must not upscale VGA");}
                    modes<<dataset<<'\t'<<name<<'\t'<<size[0]<<'\t'<<size[1]<<'\t'<<frame.height()<<'\t'<<digest(frame.pixels)<<'\n';++software;
                }
                bool rejected=false;try{scene.software_resolution(1280,720);}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Reject non-original software resolution");
            }
        }
        // The retained game objects and scheduler survive actual backend reconstruction.
        auto a=sets[0];dok::World world(dok::load_level(a,"LEVEL1"));dok::Weapons weapons(world,a);weapons.grant_all();
        const auto original_pose=*world.objects.at(*world.player).pose;const auto ticks=world.simulation_ticks();const auto maps=world.maps;const auto hp=world.objects.at(*world.player).player_state->health();
        dok::VideoSettings video;std::unique_ptr<dok::Display> display=std::make_unique<dok::Display>(video);
        for(int i=0;i<4;++i){video.renderer=i%2?dok::RendererKind::opengl:dok::RendererKind::software;video.width=640;video.height=480;auto next=std::make_unique<dok::Display>(video);display=std::move(next);display->begin();display->present();
            check(world.simulation_ticks()==ticks&&world.maps==maps&&world.objects.at(*world.player).pose->x==original_pose.x&&world.objects.at(*world.player).player_state->health()==hp,"World modified by backend rebuild");}
        std::ofstream report(out/"report.json");report<<"{\"resource_combinations\":"<<combinations<<",\"rendered_menu_frames\":"<<frames<<",\"software_scenes\":"<<software<<",\"backend_rebuilds\":4}\n";
        std::cout<<combinations<<" combinations, "<<frames<<" menu frames, "<<software<<" native software scenes, 4 backend rebuilds passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';result=1;}
    SDL_Quit();return result;
}
