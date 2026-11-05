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
        s.frame_filter=2;s.fxaa=true;s.classic_hud=true;s.auto_hud=false;
        s.resource_paths={"C:/Games/Старое золото/OLD_GOLD.RES","C:/Games/edition=two.RES"};
        s.resource_choices[1]=s.resource_paths[0];
        check(dok::save_settings(path,s,warning),"Save settings");auto loaded=dok::load_settings(path,warning);
        check(warning.empty()&&dok::encode_settings(s)==dok::encode_settings(loaded),"Full roundtrip and locale persistence");
        {std::ofstream out(path);out<<"[settings]\nversion=1\n[video]\nwidth=800\nheight=600\nbase_fov=obsolete\nforce_4_3=obsolete\n[interface]\nfps=0\n";}
        loaded=dok::load_settings(path,warning);
        check(warning.empty()&&!loaded.show_fps&&loaded.video.fullscreen_width==800&&loaded.video.fullscreen_height==600,"Ignore legacy projection keys without resetting settings");
        s.video.fullscreen_width=1280;s.video.fullscreen_height=800;
        check(dok::save_settings(path,s,warning),"Save independent fullscreen size");
        loaded=dok::load_settings(path,warning);
        check(loaded.video.width==800&&loaded.video.fullscreen_width==1280&&loaded.video.fullscreen_height==800,"Independent window/fullscreen size roundtrip");
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
        dok::AppSettings fs;fs.video.width=800;fs.video.height=600;fs.video.fullscreen_width=1280;fs.video.fullscreen_height=800;
        dok::Frontend fullscreen(fs);fullscreen.render(640,480);down(fullscreen,1);key(fullscreen,SDLK_RETURN);key(fullscreen,SDLK_RETURN);
        down(fullscreen,1);key(fullscreen,SDLK_RIGHT);down(fullscreen,1);key(fullscreen,SDLK_RIGHT);
        check(!fs.video.fullscreen&&fs.video.fullscreen_height==800,"Fullscreen selection must remain staged");
        down(fullscreen,2);key(fullscreen,SDLK_RETURN);
        check(fullscreen.video_pending()&&fs.video.fullscreen&&fs.video.width==800&&fs.video.height==600&&fs.video.fullscreen_width==1280&&fs.video.fullscreen_height==720,"Fullscreen preset must not change window size");
        key(fullscreen,SDLK_RETURN);check(!fullscreen.video_pending()&&command(fullscreen,dok::MenuCommand::keep_video),"Confirm fullscreen mode");
        down(fullscreen,2);check(fullscreen.focused_row()==8,"HUD row navigation");down(fullscreen,1);check(fullscreen.focused_row()==9,"Defaults row navigation");
        // Leave a staged mode through Escape, B and the Back row. A cancelled
        // prompt must preserve the draft and focus; discard is always explicit.
        for(int route=0;route<3;++route){
            dok::AppSettings edits;dok::Frontend staged(edits);staged.render(640,480);
            down(staged,1);key(staged,SDLK_RETURN);key(staged,SDLK_RETURN);
            down(staged,2);key(staged,SDLK_RIGHT);const auto unchanged=edits.video;
            down(staged,1);check(staged.focused_row()==4,"Aspect directly below resolution is read-only");key(staged,SDLK_UP);
            if(route==0)key(staged,SDLK_ESCAPE);
            else if(route==1)pad(staged,SDL_CONTROLLER_BUTTON_B);
            else{key(staged,SDLK_UP);key(staged,SDLK_UP);key(staged,SDLK_UP);key(staged,SDLK_RETURN);}
            check(staged.screen()==dok::MenuScreen::video_unsaved&&staged.focused_row()==2&&edits.video==unchanged,"Staged video leave prompt defaults to continue");
            key(staged,SDLK_RETURN);check(staged.screen()==dok::MenuScreen::graphics,"Continue editing");
            key(staged,SDLK_ESCAPE);key(staged,SDLK_ESCAPE);check(staged.screen()==dok::MenuScreen::graphics,"Escape preserves staged mode");
            key(staged,SDLK_ESCAPE);key(staged,SDLK_UP);key(staged,SDLK_RETURN);
            check(staged.screen()==dok::MenuScreen::settings&&edits.video==unchanged,"Explicit discard leaves without applying");
            key(staged,SDLK_RETURN);key(staged,SDLK_ESCAPE);check(staged.screen()==dok::MenuScreen::settings,"Discard clears staged mode");
            key(staged,SDLK_RETURN);down(staged,2);key(staged,SDLK_RIGHT);key(staged,SDLK_ESCAPE);
            key(staged,SDLK_UP);key(staged,SDLK_UP);staged.update(20000);key(staged,SDLK_RETURN);
            check(staged.video_pending()&&edits.video!=unchanged&&command(staged,dok::MenuCommand::apply_video),"Prompt applies preserved draft");
            staged.update(35001);check(edits.video==unchanged&&command(staged,dok::MenuCommand::revert_video),"Prompt apply retains timed rollback");
        }
        // Right click decrements values but never activates a navigation/action row.
        for(auto size:{std::array<int,2>{640,480},std::array<int,2>{1280,720}}){
            dok::AppSettings edits;dok::Frontend mouse(edits);mouse.render(size[0],size[1]);
            auto right=[&]{const auto& f=mouse.render(size[0],size[1]);const auto selected=std::find(f.pixels.begin(),f.pixels.end(),0xff302b20);
                check(selected!=f.pixels.end(),"Mouse test focus visible");const auto at=selected-f.pixels.begin();SDL_Event click{};click.type=SDL_MOUSEBUTTONDOWN;click.button.button=SDL_BUTTON_RIGHT;
                click.button.x=(int(at%f.width)+8)*size[0]/f.width;click.button.y=(int(at/f.width)+4)*size[1]/f.height;mouse.handle(click,size[0],size[1]);};
            down(mouse,1);right();check(mouse.screen()==dok::MenuScreen::main,"Right click never opens settings");key(mouse,SDLK_RETURN);key(mouse,SDLK_RETURN);
            down(mouse,2);right();key(mouse,SDLK_ESCAPE);key(mouse,SDLK_UP);key(mouse,SDLK_UP);key(mouse,SDLK_RETURN);
            check(edits.video.width==1280&&edits.video.height==800,"Right click cycles resolution backwards");key(mouse,SDLK_RETURN);
            right();check(mouse.screen()==dok::MenuScreen::graphics&&!mouse.video_pending(),"Right click cannot apply video");
            key(mouse,SDLK_ESCAPE);down(mouse,1);key(mouse,SDLK_RETURN);right();check(edits.master_volume==95,"Right click decrements volume");
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
