#include "dok/frontend.hpp"
#include "dok/display.hpp"
#include "dok/scene.hpp"
#include "dok/hud.hpp"
#include "dok/weapons.hpp"
#include "dok/png.hpp"
#include <algorithm>
#include <fstream>
#include <cstring>
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
            shot(display,menu,out/(prefix+"levels.png"));menu.main_menu();key(menu,SDLK_DOWN);key(menu,SDLK_RETURN);key(menu,SDLK_RETURN);shot(display,menu,out/(prefix+"graphics.png"));
            key(menu,SDLK_ESCAPE);key(menu,SDLK_ESCAPE);for(int i=0;i<3;++i)key(menu,SDLK_DOWN);key(menu,SDLK_RETURN);shot(display,menu,out/(prefix+"credits.png"));frames+=6;
        }
        // Readable layout and navigation checks use actual GL/software readback.
        for(int backend=0;backend<2;++backend)for(int lang=0;lang<2;++lang)for(auto size:{std::array<int,2>{1024,768},std::array<int,2>{1280,720}}){
            dok::AppSettings s;s.locale=lang?dok::LocaleId::en:dok::LocaleId::ru;s.video.renderer=backend?dok::RendererKind::opengl:dok::RendererKind::software;s.video.width=size[0];s.video.height=size[1];
            dok::Display display(s.video);dok::Frontend menu(s);menu.datasets({{&sets[2],"OLD_GOLD.RES"},{&sets[0],"PKR.RES"},{&sets[1],"PKE.RES"}});
            dok::ResourceSources sources;for(int i=0;i<3;++i)sources.add(root/(i==0?"originals/pkr/RUS/PKR.RES":i==1?"originals/pkr/ENG/PKE.RES":"originals/Old_Gold_3D/OLD_GOLD.RES"),sets[i]);menu.resources(sources);
            dok::World world(dok::load_level(sets[0],"LEVEL1"));world.map_revealed=true;menu.session_world(&world);
            const auto prefix=(backend?"ux-gl-":"ux-sw-")+std::string(lang?"en-":"ru-")+std::to_string(size[0])+"-";
            auto save=[&](const char* name){shot(display,menu,out/(prefix+name+".png"));++frames;};
            auto down=[&](int n){for(int i=0;i<n;++i)key(menu,SDLK_DOWN);};
            auto setting=[&](int index){menu.main_menu();down(1);key(menu,SDLK_RETURN);if(index>=0){down(index);key(menu,SDLK_RETURN);}};
            setting(-1);save("settings");setting(1);save("sound");setting(2);save("controls");key(menu,SDLK_RETURN);save("keyboard");down(1);key(menu,SDLK_RETURN);save("bindings");
            key(menu,SDLK_RETURN);save("key-slots");key(menu,SDLK_RETURN);save("capture");
            SDL_Event focus{};focus.type=SDL_WINDOWEVENT;focus.window.event=SDL_WINDOWEVENT_FOCUS_LOST;menu.handle(focus,size[0],size[1]);check(!menu.capturing(),"Focus must cancel binding capture");focus.window.event=SDL_WINDOWEVENT_FOCUS_GAINED;menu.handle(focus,size[0],size[1]);
            setting(2);down(1);key(menu,SDLK_RETURN);save("pad");setting(3);save("gameplay");setting(4);save("interface");key(menu,SDLK_ESCAPE);check(menu.focused_row()==4,"Back must restore parent section");
            menu.main_menu();down(3);key(menu,SDLK_RETURN);save("help");key(menu,SDLK_RETURN);save("help-page2");
            menu.main_menu();down(2);key(menu,SDLK_RETURN);save("resources");key(menu,SDLK_RETURN);save("files");key(menu,SDLK_DOWN);key(menu,SDLK_RETURN);save("path");
            const auto entered=(root/"originals/pkr/RUS/PKR.RES").u8string();const auto quoted="\""+entered+"\"";
            for(std::size_t at=0;at<quoted.size();at+=31){SDL_Event text{};text.type=SDL_TEXTINPUT;const auto n=std::min<std::size_t>(31,quoted.size()-at);std::memcpy(text.text.text,quoted.data()+at,n);menu.handle(text,size[0],size[1]);}
            key(menu,SDLK_RETURN);check(menu.screen()==dok::MenuScreen::resources&&menu.pending_resource()==entered,"Quoted direct RES path input");
            const auto pending=menu.commands();check(std::find(pending.begin(),pending.end(),dok::MenuCommand::add_resource)!=pending.end(),"Path input must request resource admission");
            menu.pause();save("pause");down(2);key(menu,SDLK_RETURN);check(menu.screen()==dok::MenuScreen::map,"Pause map route");save("map");SDL_Event back_pad{};back_pad.type=SDL_CONTROLLERBUTTONDOWN;back_pad.cbutton.button=SDL_CONTROLLER_BUTTON_B;menu.handle(back_pad,size[0],size[1]);check(menu.screen()==dok::MenuScreen::pause&&menu.focused_row()==2,"Map returns to pause row");
            menu.hide();menu.show_map();key(menu,SDLK_TAB);check(!menu.active(),"Map action toggles overlay");
            s.input.keys[static_cast<unsigned>(dok::Action::map)][0]=SDL_SCANCODE_M;menu.show_map();key(menu,SDLK_m);check(!menu.active(),"Remapped map action toggles overlay");
            menu.notice("Notice","Notice");save("notice");menu.outcome(dok::SessionPhase::retry);save("retry");menu.outcome(dok::SessionPhase::game_over);save("game-over");
            menu.main_menu();menu.datasets({});dok::ResourceSources empty;menu.resources(empty);down(2);key(menu,SDLK_RETURN);save("no-resources");menu.main_menu();down(3);key(menu,SDLK_RETURN);save("no-res-help");
        }
        std::ofstream modes(out/"software-modes.tsv");modes<<"dataset\tlevel\twidth\theight\tscene_height\tfnv64\n";
        for(int dataset=0;dataset<3;++dataset){const auto& a=sets[dataset];const int count=dataset==2?1:3;
            for(int level=1;level<=count;++level){const auto name=dataset==2?"LEVEL4":"LEVEL"+std::to_string(level);dok::World world(dok::load_level(a,name));dok::SoftwareScene scene(a,world.definition);dok::Hud hud(a);
                std::vector<std::uint8_t> vga;
                for(auto size:dok::software_modes){scene.software_resolution(size[0],size[1]);const auto& frame=scene.render(world,*world.objects.at(*world.player).pose);
                    check(frame.width()==size[0]&&frame.height()==(int(size[1]-(dataset==2?29.0f:39.0f)*(float(size[1])/200))/2)*2,"Native viewport dimensions");
                    const auto composed=hud.classic_mode(frame,size[1],*world.objects.at(*world.player).player_state);
                    check(composed.width()==size[0]&&composed.height()==size[1],"Native framebuffer dimensions");
                    if(size[0]==320)vga=frame.pixels;
                    else{std::size_t differences=0;for(int y=0;y<frame.height();++y)for(int x=0;x<frame.width();++x)differences+=frame.pixels[y*frame.width()+x]!=vga[(y*(dataset==2?170:160)/frame.height())*320+x*320/frame.width()];
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
