#include "dok/frontend.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void key(dok::Frontend& menu,SDL_Keycode code){SDL_Event e{};e.type=SDL_KEYDOWN;e.key.keysym.sym=code;e.key.keysym.scancode=code&SDLK_SCANCODE_MASK?SDL_Scancode(code&~SDLK_SCANCODE_MASK):SDL_GetScancodeFromKey(code);menu.handle(e,640,480);menu.render(640,480);}
void down(dok::Frontend& menu,int n){for(int i=0;i<n;++i)key(menu,SDLK_DOWN);}
void pad(dok::Frontend& menu,int button,bool pressed=true){SDL_Event e{};e.type=pressed?SDL_CONTROLLERBUTTONDOWN:SDL_CONTROLLERBUTTONUP;e.cbutton.button=button;menu.handle(e,640,480);menu.render(640,480);}
bool command(dok::Frontend& m,dok::MenuCommand c){const auto commands=m.commands();return std::find(commands.begin(),commands.end(),c)!=commands.end();}
}
int main(){
    SDL_SetMainReady();if(SDL_Init(SDL_INIT_EVENTS|SDL_INIT_GAMECONTROLLER)!=0)return 2;
    int result=0;
    try{
        const auto dir=std::filesystem::current_path()/"frontend-settings-test";std::filesystem::create_directories(dir);
        const auto path=dir/"settings.ini";std::error_code ec;std::filesystem::remove(path,ec);
        SDL_SetHint(SDL_HINT_PREFERRED_LOCALES,"ru_RU");check(dok::AppSettings{}.locale==dok::LocaleId::ru,"Russian OS locale");
        SDL_SetHint(SDL_HINT_PREFERRED_LOCALES,"de_DE");check(dok::AppSettings{}.locale==dok::LocaleId::en,"Other OS locale");
        SDL_ResetHint(SDL_HINT_PREFERRED_LOCALES);
        std::string warning;auto s=dok::load_settings(path,warning);check(warning.empty()&&s.locale==dok::system_locale(),"First-run locale");
        s.locale=s.locale==dok::LocaleId::ru?dok::LocaleId::en:dok::LocaleId::ru;
        s.video={dok::RendererKind::software,800,600,3,true};s.input.keyboard(dok::InputPreset::classic);s.input.gamepad(dok::InputPreset::classic);
        s.inertia=false;s.master_volume=45;s.music_volume=15;s.input.keys[0][0]=SDL_SCANCODE_I;s.input.keyboard_preset=dok::InputPreset::custom;
        s.frame_filter=2;s.fxaa=true;s.classic_hud=true;s.auto_hud=false;s.force_4_3=true;s.base_fov=90;
        s.resource_paths={"C:/Games/Старое золото/OLD_GOLD.RES","C:/Games/edition=two.RES"};
        s.resource_choices[1]=s.resource_paths[0];
        check(dok::save_settings(path,s,warning),"Save settings");auto loaded=dok::load_settings(path,warning);
        check(warning.empty()&&dok::encode_settings(s)==dok::encode_settings(loaded),"Full roundtrip and locale persistence");
        auto stored=s,before=s;before.video.renderer=dok::RendererKind::opengl;before.video.width=1920;before.locale=dok::LocaleId::en;auto after=before;after.master_volume=60;
        check(dok::merge_settings_edits(stored,before,after),"Changed persistent setting must be dirty");check(stored.video==s.video&&stored.locale==s.locale&&stored.master_volume==60,"CLI overrides leaked to disk");
        check(!dok::merge_settings_edits(stored,before,after),"Already stored edit must not write again");
        check(!dok::merge_settings_edits(stored,after,after),"Idle frame must not serialize/save settings");
        auto video_after=after;video_after.video.width=1024;
        check(!dok::merge_settings_edits(stored,after,video_after,false),"Unconfirmed video must not persist");
        check(dok::merge_settings_edits(stored,after,video_after,true),"Confirmed video must persist");
        {std::ofstream out(path);out<<"[settings]\nversion=999\n";}loaded=dok::load_settings(path,warning);check(!warning.empty()&&loaded.inertia,"Bad version fallback");
        {std::ofstream out(path);out<<"[settings]\nversion=1\n[input]\ndeadzone=nan\n";}loaded=dok::load_settings(path,warning);check(!warning.empty(),"Reject NaN");
        // Failed replacement must preserve the old destination, not unlink it first.
        const auto blocked=dir/"directory.ini";std::filesystem::create_directories(blocked);{std::ofstream out(blocked/"keep");out<<"keep";}
        check(!dok::save_settings(blocked,s,warning)&&std::filesystem::exists(blocked/"keep"),"Transactional write failure");
        s=dok::AppSettings{};dok::Frontend menu(s);menu.render(640,480);
        key(menu,SDLK_RETURN);check(menu.screen()==dok::MenuScreen::resources,"Missing RES flow");key(menu,SDLK_ESCAPE);
        down(menu,1);key(menu,SDLK_RETURN);check(menu.screen()==dok::MenuScreen::settings,"Settings route");
        key(menu,SDLK_RETURN);const auto old=s.video;key(menu,SDLK_RIGHT);down(menu,2);key(menu,SDLK_RIGHT);
        down(menu,1);menu.update(1000);key(menu,SDLK_RETURN);check(menu.video_pending()&&s.video!=old,"Apply staged video");
        check(command(menu,dok::MenuCommand::apply_video),"Video command");menu.update(16001);check(s.video==old&&!menu.video_pending()&&command(menu,dok::MenuCommand::revert_video),"15-second rollback");
        key(menu,SDLK_ESCAPE);down(menu,2);key(menu,SDLK_RETURN);key(menu,SDLK_RETURN);
        check(menu.screen()==dok::MenuScreen::keyboard,"Keyboard settings");key(menu,SDLK_RIGHT);check(s.input.keyboard_preset==dok::InputPreset::classic&&s.input.pad_preset==dok::InputPreset::modern,"Independent presets");
        down(menu,1);key(menu,SDLK_RETURN);key(menu,SDLK_RETURN);key(menu,SDLK_RETURN);check(menu.capturing(),"Capture entry");
        key(menu,SDLK_LCTRL);check(menu.screen()==dok::MenuScreen::conflict,"Binding conflict");key(menu,SDLK_ESCAPE);check(s.input.keys[0][0]==SDL_SCANCODE_UP,"Conflict cancel");
        key(menu,SDLK_RETURN);key(menu,SDLK_LCTRL);key(menu,SDLK_UP);key(menu,SDLK_RETURN);
        check(s.input.keys[0][0]==SDL_SCANCODE_LCTRL&&s.input.keys[int(dok::Action::fire)][0]==SDL_SCANCODE_UNKNOWN,"Conflict replace");
        key(menu,SDLK_ESCAPE);key(menu,SDLK_ESCAPE);key(menu,SDLK_ESCAPE);down(menu,1);key(menu,SDLK_RETURN);down(menu,1);key(menu,SDLK_RETURN);key(menu,SDLK_RETURN);
        check(menu.capturing(),"Gamepad capture");pad(menu,SDL_CONTROLLER_BUTTON_START);pad(menu,SDL_CONTROLLER_BUTTON_BACK);check(!menu.capturing(),"Gamepad-only cancel capture");
        menu.pause();down(menu,1);key(menu,SDLK_RETURN);down(menu,4);key(menu,SDLK_RETURN);auto locale=s.locale;key(menu,SDLK_RIGHT);check(s.locale!=locale,"Live menu locale");
        key(menu,SDLK_ESCAPE);key(menu,SDLK_ESCAPE);check(menu.screen()==dok::MenuScreen::pause,"Settings returns to pause");key(menu,SDLK_UP);key(menu,SDLK_RETURN);check(!menu.active()&&command(menu,dok::MenuCommand::resume),"Resume flow");
        menu.pause();menu.request_quit();key(menu,SDLK_RETURN);check(menu.screen()==dok::MenuScreen::pause,"Exit default is cancel");
        // Resizing followed by focus/hover used to change the help reservation
        // and recenter the entire panel on rows without a description.
        for(auto size:{std::array<int,2>{800,600},std::array<int,2>{1280,720},std::array<int,2>{1920,1080}}){
            dok::Frontend stable(s);key(stable,SDLK_DOWN);key(stable,SDLK_RETURN);key(stable,SDLK_RETURN);
            int anchor=-1;
            for(int row=0;row<12;++row){
                const auto& f=stable.render(size[0],size[1]);
                const auto border=std::find(f.pixels.begin(),f.pixels.end(),0xffe0e0e0);
                check(border!=f.pixels.end(),"Menu header missing");
                const int top=int(border-f.pixels.begin())/f.width;
                if(anchor<0)anchor=top;check(top==anchor,"Menu moved when focus changed after resize");
                const auto selected=std::find(f.pixels.begin(),f.pixels.end(),0xff302b20);
                check(selected!=f.pixels.end(),"Selected settings row missing");
                const auto at=selected-f.pixels.begin();SDL_Event motion{};motion.type=SDL_MOUSEMOTION;
                const int focused=stable.focused_row();
                motion.motion.x=(int(at%f.width)+8)*size[0]/f.width;motion.motion.y=(int(at/f.width)+4)*size[1]/f.height;
                stable.handle(motion,size[0],size[1]);
                check(stable.focused_row()==focused,"Hover target moved after resize");
                key(stable,SDLK_DOWN);
            }
        }
        for(auto size:{std::array<int,2>{1280,720},std::array<int,2>{960,720},std::array<int,2>{1920,1080},std::array<int,2>{2560,1440}}){
            dok::Frontend pointer(s);key(pointer,SDLK_DOWN);const auto& f=pointer.render(size[0],size[1]);
            const auto selected=std::find(f.pixels.begin(),f.pixels.end(),0xff302b20);
            check(selected!=f.pixels.end(),"Focused menu row must be visible");const auto at=selected-f.pixels.begin();
            SDL_Event click{};click.type=SDL_MOUSEBUTTONDOWN;
            click.button.button=SDL_BUTTON_LEFT;click.button.x=(int(at%f.width)+8)*size[0]/f.width;click.button.y=(int(at/f.width)+4)*size[1]/f.height;pointer.handle(click,size[0],size[1]);
            check(pointer.screen()==dok::MenuScreen::settings,"Centered menu mouse hit at both aspects");
        }
        dok::Frontend background(s);background.render(640,480);SDL_Event event{};event.type=SDL_WINDOWEVENT;event.window.event=SDL_WINDOWEVENT_FOCUS_LOST;background.handle(event,640,480);
        event.type=SDL_CONTROLLERAXISMOTION;event.caxis.axis=SDL_CONTROLLER_AXIS_LEFTY;event.caxis.value=25000;background.handle(event,640,480);
        check(background.focused_row()==0,"Background pad moved menu focus");
        event.type=SDL_WINDOWEVENT;event.window.event=SDL_WINDOWEVENT_FOCUS_GAINED;background.handle(event,640,480);
        event.type=SDL_CONTROLLERAXISMOTION;event.caxis.axis=SDL_CONTROLLER_AXIS_LEFTY;event.caxis.value=25000;background.handle(event,640,480);
        check(background.focused_row()==1,"Foreground pad axis did not recover after focus reset");
        for(auto size:{std::array<int,2>{320,200},std::array<int,2>{640,480},std::array<int,2>{1280,720},std::array<int,2>{1920,1080}}){const auto& frame=menu.render(size[0],size[1]);check(frame.pixels.size()==std::size_t(frame.width)*frame.height,"Frame dimensions");}
        std::filesystem::remove(path,ec);std::filesystem::remove(blocked/"keep",ec);std::filesystem::remove(blocked,ec);std::filesystem::remove(dir,ec);
        std::cout<<"Settings roundtrip, OS locale, CLI isolation, write failure, menus, remap, pad cancel and video rollback passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';result=1;}
    SDL_Quit();return result;
}
