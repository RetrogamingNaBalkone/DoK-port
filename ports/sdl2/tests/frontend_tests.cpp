#include "dok/frontend.hpp"
#include "dok/launch.hpp"
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
        {
            dok::LaunchArguments launch;unsigned calls=0;
            const auto platform=[&]{++calls;return dir/"platform";};
            check(dok::select_profile(launch,dir,platform).second=="platform"&&calls==1,"Default platform profile");
            std::filesystem::create_directory(dir/"portable");
            check(!dok::portable_marker(dir),"Portable directory is not a marker");
            std::filesystem::remove(dir/"portable");
            for(const auto name:{"portable","PORTABLE.txt","portable.any.extension"}){
                {std::ofstream marker(dir/name);}
                check(dok::select_profile(launch,dir,platform).second=="portable"&&calls==1,"Marker bypasses platform path");
                launch.user_directory=dir/"explicit";
                check(dok::select_profile(launch,dir,platform).first==*launch.user_directory&&calls==1,"Explicit path wins without platform lookup");
                launch.user_directory.reset();std::filesystem::remove(dir/name);
            }
            const auto absolute=std::filesystem::absolute(dir).lexically_normal();
            check(dok::store_resource_path((absolute/"games/a.res").u8string(),absolute)=="games/a.res","Internal path is relative");
            check(dok::resolve_resource_path("games/a.res",absolute)==absolute/"games/a.res","Relative path resolves from profile");
            const auto external=absolute.parent_path()/"external.res";
            check(dok::store_resource_path(external.u8string(),absolute)==external.u8string(),"External path stays absolute");
        }
        SDL_SetHint(SDL_HINT_PREFERRED_LOCALES,"ru_RU");check(dok::AppSettings{}.locale==dok::LocaleId::ru,"Russian OS locale");
        SDL_SetHint(SDL_HINT_PREFERRED_LOCALES,"de_DE");check(dok::AppSettings{}.locale==dok::LocaleId::en,"Other OS locale");
        SDL_ClearHints();
        std::string warning;auto s=dok::load_settings(path,warning);check(warning.empty()&&s.locale==dok::system_locale(),"First-run locale");
        check(!s.show_fps&&!s.fxaa&&s.texture_filter()==0&&s.frame_filter==0,"First-run presentation defaults");
        s.locale=s.locale==dok::LocaleId::ru?dok::LocaleId::en:dok::LocaleId::ru;
        s.video={dok::RendererKind::software,800,600,3,true};s.input.keyboard(dok::InputPreset::classic);s.input.gamepad(dok::InputPreset::classic);
        s.inertia=false;s.effects_volume=45;s.input.mouse_enabled=false;s.input.gamepad_enabled=false;s.music_volume=15;s.input.keys[0][0]=SDL_SCANCODE_I;s.input.keyboard_preset=dok::InputPreset::custom;
        s.frame_filter=3;s.mip_filter=5;s.saves_enabled=false;s.camera_bobbing=false;s.boundary_hits=true;s.show_fps=true;s.fxaa=true;s.classic_hud=true;s.auto_hud=false;
        s.resource_paths={"C:/Games/Старое золото/OLD_GOLD.RES","C:/Games/edition=two.RES"};
        s.resource_choices[1]=s.resource_paths[0];
        check(dok::save_settings(path,s,warning),"Save settings");auto loaded=dok::load_settings(path,warning);
        check(warning.empty()&&dok::encode_settings(s)==dok::encode_settings(loaded),"Full roundtrip and locale persistence");
        {std::ofstream out(path);out<<"[settings]\nversion=1\n[video]\nwidth=800\nheight=600\nbase_fov=obsolete\nforce_4_3=obsolete\n[interface]\nfps=0\n";}
        loaded=dok::load_settings(path,warning);
        check(loaded.camera_bobbing,"Old profiles keep native camera bobbing");
        check(!loaded.boundary_hits,"Missing optional hit setting defaults to native");
        check(warning.empty()&&!loaded.show_fps&&loaded.video.fullscreen_width==800&&loaded.video.fullscreen_height==600,"Ignore legacy projection keys without resetting settings");
        for(int mip=0;mip<=5;++mip)for(int linear=0;linear<=1;++linear){
            {std::ofstream out(path);out<<"[settings]\nversion=1\n[video]\nlinear="<<linear<<"\nmip_filter="<<mip<<"\nframe_filter=2\n[interface]\nfps=0\n[gameplay]\nsaves_enabled=0\n";}
            const auto legacy=dok::load_settings(path,warning);const bool smooth=mip||linear;
            check(warning.empty()&&legacy.texture_filter()==(smooth?6:0)&&legacy.mip_filter==(smooth?5:0),"Old GL choices migrate to nearest or Smooth");
            check(legacy.frame_filter==2&&!legacy.saves_enabled&&!legacy.show_fps,"Filter migration preserves unrelated settings");
            check(dok::save_settings(path,legacy,warning),"Save normalized filtering");
            const auto again=dok::load_settings(path,warning);check(warning.empty()&&dok::encode_settings(legacy)==dok::encode_settings(again),"Filter migration is stable across restarts");
        }
        s.video.fullscreen_width=1280;s.video.fullscreen_height=800;
        check(dok::save_settings(path,s,warning),"Save independent fullscreen size");
        loaded=dok::load_settings(path,warning);
        check(loaded.video.width==800&&loaded.video.fullscreen_width==1280&&loaded.video.fullscreen_height==800,"Independent window/fullscreen size roundtrip");
        auto stored=s,before=s;before.video.renderer=dok::RendererKind::opengl;before.video.width=1920;before.locale=dok::LocaleId::en;auto after=before;after.effects_volume=60;
        check(dok::merge_settings_edits(stored,before,after),"Changed persistent setting must be dirty");check(stored.video==s.video&&stored.locale==s.locale&&stored.effects_volume==60,"CLI overrides leaked to disk");
        check(!dok::merge_settings_edits(stored,before,after),"Already stored edit must not write again");
        check(!dok::merge_settings_edits(stored,after,after),"Idle frame must not serialize/save settings");
        auto video_after=after;video_after.video.width=1024;
        check(!dok::merge_settings_edits(stored,after,video_after,false),"Unconfirmed video must not persist");
        check(dok::merge_settings_edits(stored,after,video_after,true),"Confirmed video must persist");
        for(auto volumes:{std::array<int,3>{45,80,15},{0,100,100},{100,0,65}}){
            {std::ofstream out(path);out<<"[settings]\nversion=1\n[audio]\nmaster="<<volumes[0]<<"\neffects="<<volumes[1]<<"\nmusic="<<volumes[2]<<"\n";}
            loaded=dok::load_settings(path,warning);
            check(warning.empty()&&loaded.effects_volume==volumes[0]*volumes[1]/100&&loaded.music_volume==volumes[0]*volumes[2]/100,"Legacy audio effective levels");
            check(loaded.input.mouse_enabled&&loaded.input.gamepad_enabled,"Legacy devices default enabled");
            const auto migrated=dok::encode_settings(loaded);
            check(migrated.find("master=100")!=std::string::npos&&dok::save_settings(path,loaded,warning),"Save normalized master");
            loaded=dok::load_settings(path,warning);check(warning.empty()&&dok::encode_settings(loaded)==migrated,"Migration is idempotent");
        }
        for(int preset:{0,2}){
            {std::ofstream out(path);out<<"[settings]\nversion=1\n[input]\nkeyboard_preset="<<preset<<"\nmouse12=0\nmouse13=0\n";}
            loaded=dok::load_settings(path,warning);
            check(warning.empty()&&loaded.input.mouse[12]==(preset==0?6:0)&&loaded.input.mouse[13]==(preset==0?7:0),"Wheel defaults migrate stock profiles and preserve custom unbindings");
        }
        // Old stock classic layouts adopt the new face positions; remaps remain intact.
        for(int variant=0;variant<4;++variant){
            dok::AppSettings old;old.input.gamepad(dok::InputPreset::classic);
            auto& buttons=old.input.buttons;
            const auto fire=static_cast<unsigned>(dok::Action::fire),run=static_cast<unsigned>(dok::Action::run);
            const auto use=static_cast<unsigned>(dok::Action::use),next=static_cast<unsigned>(dok::Action::next_weapon);
            buttons[fire]=SDL_CONTROLLER_BUTTON_A;buttons[run]=SDL_CONTROLLER_BUTTON_B;
            buttons[use]=SDL_CONTROLLER_BUTTON_X;buttons[next]=SDL_CONTROLLER_BUTTON_Y;
            if(variant==1)old.input.pad_preset=dok::InputPreset::custom;
            if(variant==2)buttons[static_cast<unsigned>(dok::Action::map)]=SDL_CONTROLLER_BUTTON_RIGHTSTICK;
            if(variant==3)old.input.gamepad(dok::InputPreset::modern);
            old.input.gamepad_enabled=false;old.input.stick_deadzone=0.3f;old.show_fps=true;old.fxaa=true;old.texture_filter(6);old.frame_filter=3;
            check(dok::save_settings(path,old,warning),"Save previous pad profile");
            loaded=dok::load_settings(path,warning);check(warning.empty(),"Load previous pad profile");
            auto expected=old;if(variant==0)expected.input.gamepad(dok::InputPreset::classic);
            check(dok::encode_settings(loaded)==dok::encode_settings(expected),"Migrate stock classic only, preserving other preferences");
            check(dok::save_settings(path,loaded,warning),"Save migrated classic profile");
            const auto again=dok::load_settings(path,warning);
            check(warning.empty()&&dok::encode_settings(again)==dok::encode_settings(loaded),"Pad migration is idempotent");
        }
        {std::ofstream out(path);out<<"[settings]\nversion=1\n";}
        loaded=dok::load_settings(path,warning);
        check(warning.empty()&&!loaded.show_fps&&!loaded.fxaa&&loaded.texture_filter()==0&&loaded.frame_filter==0,"Missing INI presentation fields use defaults");
        {std::ofstream out(path);out<<"[settings]\nversion=999\n";}loaded=dok::load_settings(path,warning);check(!warning.empty()&&loaded.inertia,"Bad version fallback");
        {std::ofstream out(path);out<<"[settings]\nversion=1\n[input]\ndeadzone=nan\n";}loaded=dok::load_settings(path,warning);check(!warning.empty(),"Reject NaN");
        // Failed replacement must preserve the old destination, not unlink it first.
        const auto blocked=dir/"directory.ini";std::filesystem::create_directories(blocked);{std::ofstream out(blocked/"keep");out<<"keep";}
        check(!dok::save_settings(blocked,s,warning)&&std::filesystem::exists(blocked/"keep"),"Transactional write failure");
        s=dok::AppSettings{};dok::Frontend menu(s);menu.render(640,480);
        {
            dok::AppSettings bindings;dok::Frontend remap(bindings);remap.render(640,480);
            down(remap,1);key(remap,SDLK_RETURN);down(remap,2);key(remap,SDLK_RETURN);key(remap,SDLK_RETURN);
            down(remap,2);key(remap,SDLK_RETURN);key(remap,SDLK_RETURN);down(remap,2);key(remap,SDLK_RETURN);
            check(remap.capturing(),"Wheel binding capture entry");
            SDL_Event wheel{};wheel.type=SDL_MOUSEWHEEL;wheel.wheel.y=1;remap.handle(wheel,640,480);remap.render(640,480);
            check(remap.screen()==dok::MenuScreen::conflict,"Wheel binding detects existing weapon action");
            key(remap,SDLK_UP);key(remap,SDLK_RETURN);
            check(bindings.input.mouse[0]==6&&bindings.input.mouse[static_cast<unsigned>(dok::Action::next_weapon)]==0&&bindings.input.keyboard_preset==dok::InputPreset::custom,"Wheel replacement preserves remap semantics");
        }
        key(menu,SDLK_RETURN);check(menu.screen()==dok::MenuScreen::resources,"Missing RES flow");key(menu,SDLK_ESCAPE);
        {
            dok::AppSettings config;dok::Frontend resources(config);resources.render(640,480);
            down(resources,2);key(resources,SDLK_RETURN);down(resources,3);
            check(resources.focused_row()==3,"Exactly three resource rows plus Back");
            key(resources,SDLK_UP);key(resources,SDLK_RETURN);
            check(resources.screen()==dok::MenuScreen::files&&resources.resource_target()==dok::DatasetId::kremlin_pke,"English edition targets file picker");
            key(resources,SDLK_ESCAPE);check(resources.focused_row()==2,"File picker cancel restores game row");
            pad(resources,SDL_CONTROLLER_BUTTON_DPAD_UP);pad(resources,SDL_CONTROLLER_BUTTON_A);
            check(resources.screen()==dok::MenuScreen::files&&resources.resource_target()==dok::DatasetId::kremlin_pkr,"Pad targets Russian file picker");
            pad(resources,SDL_CONTROLLER_BUTTON_B);check(resources.focused_row()==1,"Pad cancel restores game row");
            const auto& resource_frame=resources.render(640,480);
            const auto selected=std::find(resource_frame.pixels.begin(),resource_frame.pixels.end(),0xff302b20);
            check(selected!=resource_frame.pixels.end(),"Resource row is visibly selected");
            SDL_Event click{};click.type=SDL_MOUSEBUTTONDOWN;click.button.button=SDL_BUTTON_LEFT;
            click.button.x=580;click.button.y=int(selected-resource_frame.pixels.begin())/resource_frame.width+10;
            resources.handle(click,640,480);
            check(resources.screen()==dok::MenuScreen::files&&resources.resource_target()==dok::DatasetId::kremlin_pkr,"File button mouse hit targets its game");
            key(resources,SDLK_ESCAPE);
            for(auto size:{std::array<int,2>{320,200},{480,360},{1280,720},{2560,1440}})resources.render(size[0],size[1]);
        }
        down(menu,1);key(menu,SDLK_RETURN);check(menu.screen()==dok::MenuScreen::settings,"Settings route");
        key(menu,SDLK_RETURN);const auto old=s.video;key(menu,SDLK_RIGHT);down(menu,2);key(menu,SDLK_RIGHT);
        down(menu,1);menu.update(1000);key(menu,SDLK_RETURN);check(menu.video_pending()&&s.video!=old,"Apply staged video");
        check(command(menu,dok::MenuCommand::apply_video),"Video command");menu.update(16001);check(s.video==old&&!menu.video_pending()&&command(menu,dok::MenuCommand::revert_video),"15-second rollback");
        key(menu,SDLK_ESCAPE);down(menu,2);key(menu,SDLK_RETURN);key(menu,SDLK_RETURN);
        check(menu.screen()==dok::MenuScreen::keyboard,"Keyboard settings");down(menu,1);key(menu,SDLK_RIGHT);check(s.input.keyboard_preset==dok::InputPreset::classic&&s.input.pad_preset==dok::InputPreset::modern,"Independent presets");
        down(menu,1);key(menu,SDLK_RETURN);key(menu,SDLK_RETURN);key(menu,SDLK_RETURN);check(menu.capturing(),"Capture entry");
        key(menu,SDLK_LCTRL);check(menu.screen()==dok::MenuScreen::conflict,"Binding conflict");key(menu,SDLK_ESCAPE);check(s.input.keys[0][0]==SDL_SCANCODE_UP,"Conflict cancel");
        key(menu,SDLK_RETURN);key(menu,SDLK_LCTRL);key(menu,SDLK_UP);key(menu,SDLK_RETURN);
        check(s.input.keys[0][0]==SDL_SCANCODE_LCTRL&&s.input.keys[int(dok::Action::fire)][0]==SDL_SCANCODE_UNKNOWN,"Conflict replace");
        key(menu,SDLK_ESCAPE);key(menu,SDLK_ESCAPE);key(menu,SDLK_ESCAPE);down(menu,1);key(menu,SDLK_RETURN);down(menu,2);key(menu,SDLK_RETURN);key(menu,SDLK_RETURN);
        check(menu.capturing(),"Gamepad capture");pad(menu,SDL_CONTROLLER_BUTTON_START);pad(menu,SDL_CONTROLLER_BUTTON_BACK);check(!menu.capturing(),"Gamepad-only cancel capture");
        menu.pause();down(menu,1);key(menu,SDLK_RETURN);key(menu,SDLK_RETURN);for(int n=0;n<12&&menu.focused_row()!=8;++n)key(menu,SDLK_DOWN);auto locale=s.locale;key(menu,SDLK_RIGHT);check(s.locale!=locale,"Live menu locale");
        key(menu,SDLK_ESCAPE);key(menu,SDLK_ESCAPE);check(menu.screen()==dok::MenuScreen::pause,"Settings returns to pause");key(menu,SDLK_UP);key(menu,SDLK_RETURN);check(!menu.active()&&command(menu,dok::MenuCommand::resume),"Resume flow");
        menu.pause();menu.request_quit();key(menu,SDLK_RETURN);check(menu.screen()==dok::MenuScreen::pause,"Exit default is cancel");
        {
            dok::AppSettings edits;dok::Frontend revised(edits);revised.render(640,480);
            auto focus=[&](int row){for(int n=0;n<20&&revised.focused_row()!=row;++n)key(revised,SDLK_DOWN);check(revised.focused_row()==row,"Revised row reachable");};
            down(revised,1);key(revised,SDLK_RETURN);down(revised,4);check(revised.focused_row()==4,"Four settings categories and Back");
            key(revised,SDLK_UP);key(revised,SDLK_UP);key(revised,SDLK_UP);key(revised,SDLK_UP);key(revised,SDLK_RETURN);
            focus(5);key(revised,SDLK_RIGHT);check(edits.linear&&edits.frame_filter==0,"GL filtering independent");
            check(edits.texture_filter()==6,"Smooth selects complete filtering preset");key(revised,SDLK_RIGHT);check(edits.texture_filter()==0,"Two GL choices wrap to nearest");key(revised,SDLK_LEFT);check(edits.texture_filter()==6,"Reverse toggle selects Smooth");
            focus(0);key(revised,SDLK_RIGHT);focus(5);key(revised,SDLK_LEFT);check(edits.frame_filter==3,"Experimental xBR reachable");key(revised,SDLK_LEFT);
            check(edits.linear&&edits.frame_filter==2,"Software Sharp Bilinear independent");
            key(revised,SDLK_DOWN);check(revised.focused_row()==7,"Software skips disabled FXAA");
            focus(0);key(revised,SDLK_RIGHT);focus(5);check(edits.frame_filter==2&&edits.linear,"Filter values survive renderer roundtrip");
            const auto locale=edits.locale;focus(8);key(revised,SDLK_RIGHT);check(edits.locale!=locale&&revised.focused_row()==8,"Language preserves semantic focus");
            focus(9);key(revised,SDLK_RIGHT);check(edits.show_fps,"FPS applies immediately");
            focus(10);key(revised,SDLK_RETURN);check(edits.locale==dok::system_locale()&&!edits.show_fps&&!edits.fxaa&&edits.texture_filter()==0&&edits.frame_filter==0,"Combined defaults");
            key(revised,SDLK_ESCAPE);down(revised,2);key(revised,SDLK_RETURN);key(revised,SDLK_RETURN);
            key(revised,SDLK_RETURN);check(!edits.input.mouse_enabled,"Disable mouse gameplay");
            pad(revised,SDL_CONTROLLER_BUTTON_DPAD_DOWN);key(revised,SDLK_RETURN);
            check(!edits.input.mouse_enabled&&edits.input.keyboard_preset==dok::InputPreset::classic,"Preset preserves mouse switch and pad menu navigation");
            focus(4);key(revised,SDLK_RETURN);check(edits.input.mouse_enabled,"Mouse defaults enable input");
            key(revised,SDLK_ESCAPE);down(revised,1);key(revised,SDLK_RETURN);
            key(revised,SDLK_RETURN);check(!edits.input.gamepad_enabled,"Disable gamepad gameplay");
            pad(revised,SDL_CONTROLLER_BUTTON_DPAD_DOWN);pad(revised,SDL_CONTROLLER_BUTTON_A);
            check(!edits.input.gamepad_enabled&&edits.input.pad_preset==dok::InputPreset::classic,"Disabled pad still navigates menus");
            focus(5);pad(revised,SDL_CONTROLLER_BUTTON_A);check(edits.input.gamepad_enabled,"Pad defaults enable input");
            auto saved=edits,old=edits;edits.input.mouse_enabled=false;edits.input.gamepad_enabled=false;
            check(dok::merge_settings_edits(saved,old,edits)&&!saved.input.mouse_enabled&&!saved.input.gamepad_enabled,"Device toggles persist through merge");
        }
        {
            dok::AppSettings auth;dok::Frontend m(auth);m.render(640,480);
            key(m,SDLK_DOWN);key(m,SDLK_RETURN);down(m,3);key(m,SDLK_RETURN);
            check(m.screen()==dok::MenuScreen::gameplay,"Gameplay screen");key(m,SDLK_DOWN);key(m,SDLK_LEFT);
            check(!auth.saves_enabled&&m.focused_row()==1,"Saving is an adjustable authenticity switch");
            m.quickload_request();check(!command(m,dok::MenuCommand::quickload),"Disabled quickload command");
            check(dok::save_settings(path,auth,warning),"Persist saving switch");auto round=dok::load_settings(path,warning);check(warning.empty()&&!round.saves_enabled,"Saving switch survives restart");
            m.outcome(dok::SessionPhase::retry);m.render(640,480);down(m,2);check(m.focused_row()<2,"Death screen skips disabled loads");
            m.pause();m.render(640,480);down(m,2);check(m.focused_row()==7,"Pause skips all unavailable save/load commands");
            auth.saves_enabled=true;m.quickload_request();check(command(m,dok::MenuCommand::quickload),"Reenable quickload");
        }
        {
            dok::AppSettings auth;dok::Frontend m(auth);m.render(640,480);
            key(m,SDLK_DOWN);key(m,SDLK_RETURN);down(m,3);key(m,SDLK_RETURN);down(m,2);
            const auto before=auth;
            key(m,SDLK_LEFT);check(!auth.camera_bobbing&&m.focused_row()==2,"Bobbing switch adjusts with arrows");
            auto stored=before;
            check(dok::merge_settings_edits(stored,before,auth)&&!stored.camera_bobbing,"Bobbing edits persist through merge");
            check(dok::save_settings(path,stored,warning),"Save bobbing switch");
            check(!dok::load_settings(path,warning).camera_bobbing&&warning.empty(),"Disabled bobbing survives restart");
            pad(m,SDL_CONTROLLER_BUTTON_A);check(auth.camera_bobbing,"Pad toggles bobbing on");
            key(m,SDLK_RETURN);check(!auth.camera_bobbing,"Enter toggles bobbing off");
            down(m,2);key(m,SDLK_RETURN);check(auth.camera_bobbing,"Gameplay defaults restore native bobbing");
            check(dok::save_settings(path,auth,warning)&&dok::load_settings(path,warning).camera_bobbing&&warning.empty(),"Enabled bobbing survives restart");
        }
        {
            dok::AppSettings auth;dok::Frontend m(auth);m.render(640,480);
            check(!auth.boundary_hits,"Expanded hits are opt-in");
            key(m,SDLK_DOWN);key(m,SDLK_RETURN);down(m,3);key(m,SDLK_RETURN);down(m,3);
            const auto before=auth;key(m,SDLK_LEFT);check(auth.boundary_hits,"Boundary-hit menu toggle");
            auto stored=before;check(dok::merge_settings_edits(stored,before,auth)&&stored.boundary_hits,"Boundary-hit persistence merge");
            check(dok::save_settings(path,stored,warning)&&dok::load_settings(path,warning).boundary_hits&&warning.empty(),"Boundary-hit restart");
            key(m,SDLK_DOWN);key(m,SDLK_RETURN);check(!auth.boundary_hits,"Defaults restore native hit behavior");
        }
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
        down(fullscreen,1);key(fullscreen,SDLK_RETURN);
        check(fullscreen.video_pending()&&fs.video.fullscreen&&fs.video.width==800&&fs.video.height==600&&fs.video.fullscreen_width==1280&&fs.video.fullscreen_height==720,"Fullscreen preset must not change window size");
        key(fullscreen,SDLK_RETURN);check(!fullscreen.video_pending()&&command(fullscreen,dok::MenuCommand::keep_video),"Confirm fullscreen mode");
        for(int n=0;n<12&&fullscreen.focused_row()!=6;++n)key(fullscreen,SDLK_DOWN);check(fullscreen.focused_row()==6,"FXAA row navigation");down(fullscreen,4);check(fullscreen.focused_row()==10,"Defaults row navigation");
        {
            dok::AppSettings edits;dok::Frontend staged(edits);staged.render(640,480);
            down(staged,1);key(staged,SDLK_RETURN);key(staged,SDLK_RETURN);
            down(staged,3);check(staged.focused_row()==5,"Unchanged video skips disabled Apply");
            key(staged,SDLK_RIGHT);key(staged,SDLK_UP);key(staged,SDLK_DOWN);
            check(staged.focused_row()==5&&!command(staged,dok::MenuCommand::apply_video),"Instant filtering does not enable Apply");
            key(staged,SDLK_UP);key(staged,SDLK_UP);key(staged,SDLK_RIGHT);
            down(staged,2);check(staged.focused_row()==4,"Changed window mode enables Apply");
            key(staged,SDLK_UP);key(staged,SDLK_UP);key(staged,SDLK_LEFT);
            down(staged,2);check(staged.focused_row()==5,"Restoring window mode disables Apply");
            key(staged,SDLK_UP);key(staged,SDLK_UP);key(staged,SDLK_RIGHT);down(staged,2);
            key(staged,SDLK_RETURN);check(staged.video_pending()&&command(staged,dok::MenuCommand::apply_video),"Changed mode applies");
            key(staged,SDLK_RETURN);check(!staged.video_pending()&&command(staged,dok::MenuCommand::keep_video),"Confirmed mode returns to graphics");
            down(staged,3);check(staged.focused_row()==5,"Confirmed mode disables Apply");
        }
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
            for(int n=0;n<12&&mouse.focused_row()!=11;++n)key(mouse,SDLK_DOWN);
            right();check(mouse.screen()==dok::MenuScreen::graphics&&!mouse.video_pending(),"Right click cannot activate action rows");
            key(mouse,SDLK_ESCAPE);down(mouse,1);key(mouse,SDLK_RETURN);right();check(edits.effects_volume==95,"Right click decrements volume");
        }
        {dok::AppSettings errors;dok::Frontend returning(errors);returning.hide();returning.notice("error","error");returning.render(640,480);key(returning,SDLK_ESCAPE);
            check(returning.screen()==dok::MenuScreen::hidden&&command(returning,dok::MenuCommand::resume),"Notice dismissed to live game must resume input");
            returning.pause();returning.notice("error","error");returning.render(640,480);key(returning,SDLK_ESCAPE);check(returning.screen()==dok::MenuScreen::pause&&!command(returning,dok::MenuCommand::resume),"Notice dismissed to pause must keep input paused");}
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
