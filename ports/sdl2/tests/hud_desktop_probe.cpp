#include "dok/hud.hpp"
#include "dok/scene.hpp"
#include "dok/weapons.hpp"
#include "dok/cheat.hpp"
#include "dok/gl_renderer.hpp"
#include <iostream>
#include <stdexcept>

static void check(bool okay,const char* why){if(!okay)throw std::runtime_error(why);}
int main(int argc,char** argv){
    SDL_SetMainReady();SDL_Window* window=nullptr;
    try{
        if(argc!=3)throw std::runtime_error("Usage: dok_hud_desktop_probe RES OUTPUT_PREFIX");
        dok::CheatSequence code;int matches=0;
        for(char c:std::string("IDKFXIIDKFAidkfa"))matches+=code.push(c);
        check(matches==2,"Cheat sequence overlap/case");
        code.push('I');code.push('D');code.reset();check(!code.push('K')&&!code.push('F')&&!code.push('A'),"Cheat reset");
        const auto archive=dok::Archive::open(std::filesystem::u8path(argv[1]));
        for(const auto* level:{"LEVEL1","LEVEL2","LEVEL3","LEVEL4"}){
            if(!archive.find(level))continue;
            dok::World check_world(dok::load_level(archive,level));dok::Weapons check_weapons(check_world,archive);
            check_weapons.grant_all();const auto& p=*check_world.objects.at(*check_world.player).player_state;
            check(p.keys()==7&&p.weapons[1]&&p.weapons[2],"IDKFA inventory on another level");
        }
        dok::World world(dok::load_level(archive,archive.identify()==dok::DatasetId::old_gold?"LEVEL4":"LEVEL1"));
        dok::SoftwareScene scene(archive,world.definition);dok::Weapons weapons(world,archive);dok::Hud hud(archive);
        auto& player=*world.objects.at(*world.player).player_state;
        const auto original_health=player.health();const auto original_map=world.maps;
        weapons.grant_all();
        check(player.keys()==7&&player.weapons[1]&&player.weapons[2],"IDKFA did not grant complete inventory");
        check(player.health()==original_health&&world.maps==original_map,"IDKFA changed health/map");
        const auto maximum=player.max_charge();
        player.attributes[1]=0;
        weapons.grant_all();check(player.charge()==maximum&&player.max_charge()==maximum,"Repeat cheat must refill without increasing capacity");
        check(world.visible_message()=="\x80\xe5 \xe2\xeb \xe5\xa8\xe2\xe0\xeb\xa9 \xa4\xe3\xac\xa5\xe0!","Wrong CP866 cheat message");
        auto advance=[&](std::uint64_t ns){world.advance_events(ns,[&](dok::OwnerId owner,dok::CallbackId callback){
            auto e=static_cast<dok::GameEvent>(callback);
            if(e==dok::GameEvent::weapon_tick||e==dok::GameEvent::weapon_switch)weapons.event(owner,e);
        });};
        advance(500000000);
        check(weapons.fire([](std::uint8_t){return std::int16_t(1);}),"Granted weapon cannot fire");
        advance(1000000000);
        check(weapons.select(2),"Cannot select second granted weapon");advance(1000000000);
        check(player.current_weapon==2,"Weapon switch failed");
        weapons.grant_all();check(player.current_weapon==2,"Repeated cheat changed equipped weapon");
        // Compare reuse against forced repaint with identical animation history.
        dok::Hud cached(archive),repaint(archive);
        auto sample=player;auto colors=scene.palette();
        for(int step=0;step<80;++step){
            const double time=step*0.05;
            if(step==4)sample.attributes[0]-=7;
            if(step==8)sample.current_weapon=1;
            if(step==12)sample.attributes[1]-=3;
            if(step==16)sample.attributes[2]+=10;
            if(step==20)sample.attributes[10]=1;
            if(step==24)sample.score+=1;
            if(step==28)colors[2]^=0x00112345;
            if(step==32)sample.weapons[2]=0;
            const std::string message=step<36?"":"changed";
            const int width=step<40?1024:800;
            repaint.desktop(width+1,sample,colors,message,time);
            const auto expected=repaint.desktop(width,sample,colors,message,time).pixels;
            check(cached.desktop(width,sample,colors,message,time).pixels==expected,"HUD cache froze state/animation");
            check(cached.desktop(width,sample,colors,message,time).pixels==expected,"HUD redraw differs");
        }
        check(SDL_Init(SDL_INIT_VIDEO)==0,"SDL init");
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,2);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,0);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,24);
        window=SDL_CreateWindow("HUD desktop acceptance",0,0,1280,720,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
        check(window,"Window creation");
        {
            dok::GlRenderer gl(window);
            auto draw=[&](const std::string& suffix,const std::string& message,double time){
                gl.begin();gl.world(scene.palette());
                scene.render(world,*world.objects.at(*world.player).pose,{},[&](const auto& vertices,const auto& image,dok::Bytes light,dok::RasterAxis axis){gl.polygon(vertices,image,light,axis);},gl.view_width());
                dok::IndexedSurface overlay(gl.view_width(),160);weapons.overlay(overlay);
                std::vector<std::uint32_t> pixels(overlay.pixels.size());
                for(std::size_t i=0;i<pixels.size();++i)if(overlay.pixels[i])pixels[i]=scene.palette()[overlay.pixels[i]];
                gl.image(pixels,overlay.width(),overlay.height(),{0,0,320,240});
                auto frame=hud.desktop(1280,player,scene.palette(),message,time);
                check(frame.height==100&&frame.pixels.size()==128000,"HUD bounds");
                for(int x=0;x<1280;++x)check(frame.pixels[(frame.height-1)*1280+x]==0,"HUD bottom margin");
                gl.image(frame.pixels,frame.width,frame.height,{0,720-frame.height,1280,frame.height},true);
                gl.screenshot(std::string(argv[2])+suffix+".png");
            };
            draw("-full",world.visible_message(),3);
            player.current_weapon=1;hud.desktop(1280,player,scene.palette(),"",4);
            draw("-switch",world.visible_message(),4.09);
            player.attributes[0]=10;
            draw("-critical",std::string(38,'M'),5);
            player.attributes[0]=32767;player.attributes[1]=32767;
            for(int width:{640,800,1280,1920}){
                const auto frame=hud.desktop(width,player,scene.palette(),std::string(38,'W'),6);
                check(frame.width==width&&frame.pixels.size()==std::size_t(width*100),"HUD extreme values");
            }
        }
        SDL_DestroyWindow(window);SDL_Quit();
        std::cout<<"IDKFA sequence, keys/weapons, idempotence, fire/switch; desktop GL HUD captures passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';if(window)SDL_DestroyWindow(window);SDL_Quit();return 1;}
}
