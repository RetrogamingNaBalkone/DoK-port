#include "dok/resources.hpp"
#include "dok/scene.hpp"
#include "dok/movement.hpp"
#include "dok/sdl_input.hpp"
#include "dok/interactions.hpp"
#include "dok/items.hpp"
#include "dok/weapons.hpp"
#include "dok/monsters.hpp"
#include "dok/flow.hpp"
#include "dok/hud.hpp"
#include "dok/sdl_audio.hpp"
#include "dok/display.hpp"
#include "dok/frontend.hpp"
#include "dok/view_motion.hpp"
#include "dok/cheat.hpp"
#include <chrono>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace {
struct SdlEnd {~SdlEnd(){SDL_Quit();}};
struct Loaded {std::filesystem::path path;dok::Archive archive;};
struct ScriptEvent {unsigned frame;std::string action,value;};
}
int main(int argc,char** argv){
    SDL_SetMainReady();if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER|SDL_INIT_AUDIO)!=0){std::cerr<<SDL_GetError()<<'\n';return 1;}SdlEnd sdl;
    try{
        char* pref=SDL_GetPrefPath("DoK-port","SourcePort");if(!pref)throw std::runtime_error(SDL_GetError());
        std::filesystem::path user_path=std::filesystem::u8path(pref);SDL_free(pref);
        for(int i=1;i+1<argc;++i)if(std::string(argv[i])=="--user-dir")user_path=std::filesystem::u8path(argv[++i]);
        std::filesystem::create_directories(user_path);std::ofstream log(user_path/"port.log");
        std::string warning;auto stored=dok::load_settings(user_path/"settings.ini",warning);auto settings=stored;
        unsigned frame_limit=0;std::string screenshot,level_name,script_path;std::vector<std::string> paths;bool skip_intro=false;
        for(int i=1;i<argc;++i){std::string arg=argv[i];
            auto value=[&]()->std::string{if(i+1>=argc)throw std::runtime_error("Missing value for "+arg);return argv[++i];};
            if(arg=="--res")paths.push_back(value());else if(arg=="--frames")frame_limit=std::stoul(value());
            else if(arg=="--screenshot")screenshot=value();else if(arg=="--level")level_name=value();else if(arg=="--skip-intro")skip_intro=true;
            else if(arg=="--user-dir")value();else if(arg=="--ui-script")script_path=value();
            else if(arg=="--renderer"){const auto mode=value();if(mode!="gl"&&mode!="software")throw std::runtime_error("Renderer: gl|software");settings.video.renderer=mode=="gl"?dok::RendererKind::opengl:dok::RendererKind::software;}
            else if(arg=="--filter"){const auto mode=value();if(mode!="nearest"&&mode!="linear")throw std::runtime_error("Filter: nearest|linear");settings.linear=mode=="linear";}
            else if(arg=="--locale"){const auto lang=value();if(lang!="ru"&&lang!="en")throw std::runtime_error("Locale: ru|en");settings.locale=lang=="ru"?dok::LocaleId::ru:dok::LocaleId::en;}
            else if(arg=="--size"||arg=="--software-mode"){
                const auto size=value();const auto x=size.find('x');if(x==std::string::npos)throw std::runtime_error("Size: WIDTHxHEIGHT");
                const int w=std::stoi(size.substr(0,x)),h=std::stoi(size.substr(x+1));if(w<320||h<200||w>7680||h>4320)throw std::runtime_error("Invalid window size");
                if(arg=="--size"){settings.video.width=w;settings.video.height=h;}
                else{int index=-1;for(int n=0;n<int(dok::software_modes.size());++n)if(dok::software_modes[n]==std::array<int,2>{w,h})index=n;
                    if(index<0)throw std::runtime_error("Unsupported original software mode");settings.video.software_mode=index;}
            }else throw std::runtime_error("Unknown option: "+arg);
        }
        std::vector<ScriptEvent> script;
        if(!script_path.empty()){std::ifstream f(std::filesystem::u8path(script_path));if(!f)throw std::runtime_error("Cannot read UI script");std::string line;
            while(std::getline(f,line)){if(line.empty()||line[0]=='#')continue;std::istringstream in(line);ScriptEvent event{};in>>event.frame>>event.action;std::getline(in>>std::ws,event.value);script.push_back(event);}}
        std::unique_ptr<dok::Display> display;
        try{display=std::make_unique<dok::Display>(settings.video);}catch(const std::exception& e){
            if(settings.video.renderer!=dok::RendererKind::opengl)throw;log<<"OpenGL fallback: "<<e.what()<<'\n';settings.video.renderer=dok::RendererKind::software;
            display=std::make_unique<dok::Display>(settings.video);warning="OpenGL unavailable; software renderer selected.";
        }
        SDL_ShowWindow(display->window());dok::SdlInput input(settings.input);dok::Frontend menu(settings);
        std::vector<Loaded> sets;std::string resource_errors;
        auto refresh_sets=[&]{std::vector<dok::FrontendDataset> items;for(auto& set:sets)items.push_back({&set.archive,set.path.filename().u8string()});menu.datasets(std::move(items),resource_errors);};
        auto load=[&](const std::string& path){try{
            auto a=dok::Archive::open(std::filesystem::u8path(path));if(a.identify()==dok::DatasetId::unknown)throw std::runtime_error("Unknown RES catalog");
            for(const auto& set:sets)if(set.archive.identify()==a.identify())throw std::runtime_error("Dataset already loaded");
            log<<"Loaded "<<dok::dataset_name(a.identify())<<": "<<path<<'\n';sets.push_back({std::filesystem::u8path(path),std::move(a)});
            std::stable_sort(sets.begin(),sets.end(),[](const auto& a,const auto& b){auto rank=[](dok::DatasetId id){return id==dok::DatasetId::kremlin_pkr?0:id==dok::DatasetId::kremlin_pke?1:2;};return rank(a.archive.identify())<rank(b.archive.identify());});
        }catch(const std::exception& e){resource_errors+=std::filesystem::u8path(path).filename().u8string()+": "+e.what()+"\n";log<<resource_errors;}refresh_sets();};
        if(paths.empty()){char* base=SDL_GetBasePath();if(base){const auto directory=std::filesystem::u8path(base);SDL_free(base);
            for(const auto* name:{"PKR.RES","PKE.RES","OLD_GOLD.RES"})if(std::filesystem::exists(directory/name))paths.push_back((directory/name).u8string());}}
        for(const auto& path:paths)load(path);
        std::size_t current=menu.dataset_index();
        std::unique_ptr<dok::World> world;std::unique_ptr<dok::SoftwareScene> scene;std::unique_ptr<dok::Weapons> weapons;
        std::unique_ptr<dok::Projectiles> projectiles;std::unique_ptr<dok::Monsters> monsters;std::unique_ptr<dok::GameFlow> flow;
        std::unique_ptr<dok::Hud> hud;std::unique_ptr<dok::SdlAudio> audio,music;
        dok::ViewMotion view_motion;dok::CheatSequence cheat;std::vector<std::uint32_t> scene_pixels;
        bool running=true,use_pending=false,use_previous=false;
        auto clear_world=[&]{audio.reset();hud.reset();flow.reset();monsters.reset();projectiles.reset();weapons.reset();scene.reset();world.reset();view_motion.reset();cheat.reset();input.clear();input.capture_mouse(false);};
        auto start=[&](const std::string& level){
            clear_world();music.reset();current=menu.dataset_index();const auto& a=sets.at(current).archive;level_name=level;
            world=std::make_unique<dok::World>(dok::load_level(a,level));weapons=std::make_unique<dok::Weapons>(*world,a);
            projectiles=std::make_unique<dok::Projectiles>(*world,a);monsters=std::make_unique<dok::Monsters>(*world,a,*projectiles);
            flow=std::make_unique<dok::GameFlow>(*world);hud=std::make_unique<dok::Hud>(a);scene=std::make_unique<dok::SoftwareScene>(a,world->definition);
            try{audio=std::make_unique<dok::SdlAudio>(a);world->audio=[&](const dok::AudioCommand& c){audio->command(c);};}catch(const std::exception& e){log<<"Audio unavailable: "<<e.what()<<'\n';}
            menu.hide();input.clear();input.capture_mouse(true);use_pending=use_previous=false;log<<"Started "<<dok::dataset_name(a.identify())<<' '<<level<<'\n';
        };
        if(!level_name.empty()&&!sets.empty()){
            if(level_name=="LEVEL4")for(std::size_t i=0;i<sets.size();++i)if(sets[i].archive.identify()==dok::DatasetId::old_gold)menu.select_dataset(i);
            if(level_name.size()!=6||level_name.substr(0,5)!="LEVEL"||level_name[5]<'1'||level_name[5]>'4')throw std::runtime_error("Invalid level");
            menu.preview_level(level_name[5]-'0');if(skip_intro)start(level_name);
        }
        if(!warning.empty()){log<<"Settings/display notice: "<<warning<<'\n';menu.notice("Настройки восстановлены или OpenGL недоступен. Подробности в port.log.","Settings restored or OpenGL unavailable. See port.log for details.");}
        std::string save_error;
        auto persist=[&]{if(!dok::save_settings(user_path/"settings.ini",stored,save_error)){log<<"Settings save failed: "<<save_error<<'\n';menu.notice("Не удалось сохранить настройки. Проверьте доступ к папке пользователя.","Could not save settings. Check access to the user directory.");}};
        if(!std::filesystem::exists(user_path/"settings.ini"))persist();
        unsigned frames=0,fps_frames=0,fps_value=0;auto previous_time=SDL_GetPerformanceCounter(),fps_time=previous_time;
        while(running){
            const auto before=settings;const auto stored_before=dok::encode_settings(stored);bool flow_changed=false,capture_requested=false;
            bool video_changed=false;std::vector<std::string> captures;menu.update(SDL_GetTicks64());
            for(const auto& event:script)if(event.frame==frames){
                if(event.action=="state"){
                    const auto size=display->size();log<<"STATE "<<event.value<<" renderer="<<int(settings.video.renderer)<<" size="<<size[0]<<'x'<<size[1]<<" menu="<<int(menu.screen());
                    if(world){const auto& p=*world->objects.at(*world->player).pose;const auto& player=*world->objects.at(*world->player).player_state;
                        std::uint64_t hash=14695981039346656037ULL;for(const auto& map:world->maps)for(auto byte:map.second){hash^=static_cast<unsigned char>(byte);hash*=1099511628211ULL;}
                        log<<" world="<<world.get()<<" ticks="<<world->simulation_ticks()<<" pose="<<p.x<<','<<p.y<<','<<p.z<<','<<p.heading<<" hp="<<player.health()<<" charge="<<player.charge()<<" maps="<<hash;
                    }log<<'\n';
                }
                else if(event.action=="shot")captures.push_back(event.value);
                else if(event.action=="key"){SDL_Event e{};e.type=SDL_KEYDOWN;e.key.keysym.scancode=SDL_GetScancodeFromName(event.value.c_str());e.key.keysym.sym=SDL_GetKeyFromScancode(e.key.keysym.scancode);SDL_PushEvent(&e);e.type=SDL_KEYUP;SDL_PushEvent(&e);}
                else if(event.action=="pad"){SDL_Event e{};e.type=SDL_CONTROLLERBUTTONDOWN;e.cbutton.button=std::stoi(event.value);SDL_PushEvent(&e);e.type=SDL_CONTROLLERBUTTONUP;SDL_PushEvent(&e);}
                else if(event.action=="quit"){SDL_Event e{};e.type=SDL_QUIT;SDL_PushEvent(&e);}
            }
            SDL_Event e;
            while(SDL_PollEvent(&e)){
                if(e.type==SDL_WINDOWEVENT&&e.window.windowID!=SDL_GetWindowID(display->window()))continue;
                input.handle(e);
                if(e.type==SDL_QUIT){if(world){menu.pause();menu.request_quit();}else running=false;}
                if(e.type==SDL_WINDOWEVENT&&e.window.event==SDL_WINDOWEVENT_FOCUS_LOST){input.clear();cheat.reset();if(world&&!menu.active())menu.pause();input.capture_mouse(false);}
                if(e.type==SDL_DROPFILE){const std::string path=e.drop.file;SDL_free(e.drop.file);
                    if(world)menu.notice("Добавьте ресурсы из главного меню.","Add game resources from the main menu.");else{music.reset();load(path);}continue;}
                const bool was_menu=menu.active();
                if(e.type==SDL_KEYDOWN&&!e.key.repeat&&!menu.capturing()){
                    if(e.key.keysym.sym==SDLK_F12)capture_requested=true;
                    if(e.key.keysym.sym==SDLK_F3)settings.show_fps=!settings.show_fps;
                    if(e.key.keysym.sym==SDLK_F4)settings.linear=!settings.linear;
                    if(e.key.keysym.sym==SDLK_F11&&!menu.video_pending()){
                        const bool next=!settings.video.fullscreen;
                        if(SDL_SetWindowFullscreen(display->window(),next?SDL_WINDOW_FULLSCREEN_DESKTOP:0)==0){settings.video.fullscreen=next;stored.video.fullscreen=next;menu.window_mode_changed(next);}
                    }
                }
                if(was_menu){int w,h;SDL_GetWindowSize(display->window(),&w,&h);menu.handle(e,w,h);input.clear();use_pending=false;cheat.reset();flow_changed=true;}
                else if(e.type==SDL_KEYDOWN&&!e.key.repeat){
                    if(e.key.keysym.sym==SDLK_ESCAPE){menu.pause();input.clear();input.capture_mouse(false);flow_changed=true;}
                    else if(world&&!(e.key.keysym.mod&(KMOD_CTRL|KMOD_ALT|KMOD_GUI))){const auto key=e.key.keysym.scancode;
                        if(key>=SDL_SCANCODE_A&&key<=SDL_SCANCODE_Z&&cheat.push(char('A'+key-SDL_SCANCODE_A))){weapons->grant_all();log<<"IDKFA\n";}}
                }
            }
            for(const auto command:menu.commands()){
                flow_changed=true;
                switch(command){
                case dok::MenuCommand::start:try{start(menu.level()==4?"LEVEL4":"LEVEL"+std::to_string(menu.level()));}catch(const std::exception& e){clear_world();menu.main_menu();menu.notice("Не удалось загрузить уровень. Подробности в port.log.","Could not load level. See port.log.");log<<e.what()<<'\n';}break;
                case dok::MenuCommand::resume:input.clear();input.capture_mouse(true);break;
                case dok::MenuCommand::main:clear_world();break;
                case dok::MenuCommand::quit:running=false;break;
                case dok::MenuCommand::retry:if(flow&&flow->retry()){view_motion.reset();input.clear();input.capture_mouse(true);}break;
                case dok::MenuCommand::music_stop:music.reset();break;
                case dok::MenuCommand::music_title:case dok::MenuCommand::music_story:
                    music.reset();if(!sets.empty())try{music=std::make_unique<dok::SdlAudio>(sets[menu.dataset_index()].archive);music->volume(settings.master_volume*settings.music_volume/100);music->command({dok::AudioAction::once,1,dok::cp866("ЗАСТАВКА_ЗВУК")});}catch(const std::exception& e){log<<"Music unavailable: "<<e.what()<<'\n';}break;
                case dok::MenuCommand::apply_video:
                    try{auto next=std::make_unique<dok::Display>(settings.video);input.capture_mouse(false);display=std::move(next);SDL_ShowWindow(display->window());video_changed=true;}
                    catch(const std::exception& e){log<<"Video rejected: "<<e.what()<<'\n';menu.video_failed(e.what());}break;
                case dok::MenuCommand::keep_video:{auto old=settings;old.video=menu.previous_video();dok::merge_settings_edits(stored,old,settings);break;}
                case dok::MenuCommand::revert_video:
                    try{auto next=std::make_unique<dok::Display>(settings.video);display=std::move(next);SDL_ShowWindow(display->window());video_changed=true;}
                    catch(const std::exception& e){log<<"Video rollback failed: "<<e.what()<<'\n';settings.video.renderer=dok::RendererKind::software;settings.video.fullscreen=false;display=std::make_unique<dok::Display>(settings.video);SDL_ShowWindow(display->window());menu.notice("Возврат видеорежима не удался. Включён software.","Video rollback failed. Software renderer enabled.");}break;
                }
            }
            dok::merge_settings_edits(stored,before,settings,false);
            if(dok::encode_settings(stored)!=stored_before)persist();
            input.bindings=settings.input;
            if(audio){audio->volume(settings.master_volume*settings.effects_volume/100);audio->pause(menu.active());}
            if(music)music->volume(settings.master_volume*settings.music_volume/100);
            const auto now=SDL_GetPerformanceCounter();const auto elapsed_ns=static_cast<std::uint64_t>(double(now-previous_time)*1e9/SDL_GetPerformanceFrequency());previous_time=now;
            const auto actions=input.sample();
            if(world&&!menu.active()&&actions.pause){menu.pause();input.clear();input.capture_mouse(false);flow_changed=true;}
            if(actions.use&&!use_previous)use_pending=true;use_previous=actions.use;
            if(menu.active()){use_pending=false;input.capture_mouse(false);view_motion.reset();}
            if(world&&!menu.active()&&!flow_changed&&flow->phase()==dok::SessionPhase::playing){
                if(actions.select_weapon>=0)weapons->select(actions.select_weapon);if(actions.next_weapon)weapons->cycle(1);if(actions.previous_weapon)weapons->cycle(-1);
            dok::apply_mouse_yaw(*world,actions.yaw_delta);
                world->advance_events(elapsed_ns,[&](dok::OwnerId owner,dok::CallbackId callback){
                    const auto event=static_cast<dok::GameEvent>(callback);
                    if(event==dok::GameEvent::player_tick){
                        if(world->objects.at(*world->player).player_state->health()<=0||world->level_complete)return;
                        // Gameplay activation uses the original simulation region;
                        // backend, window size and frame timing must not widen it.
                        monsters->consider(dok::original_cell_bounds(*world->objects.at(*world->player).pose,
                            world->definition.cell_side,world->definition.width,world->definition.length));
                        if(actions.fire)weapons->fire([&](std::uint8_t glyph){return projectiles->spawn(owner,glyph);});
                        if(use_pending){
                            const auto result=dok::player_use(*world);use_pending=false;
                            if(result==dok::UseResult::locked)log<<"Door locked: required key missing\n";
                            if(result==dok::UseResult::queue_full)log<<"Door event queue full\n";
                        }
                        if(const auto item=dok::player_pickup(*world)){
                            log<<"Picked object "<<item<<'\n';
                            if(world->object_for_owner(item).weapon)weapons->picked_up(item);
                        }
                        dok::player_sensors(*world);
                        auto movement=actions;
                        // Interpolate the entire player displacement, including currents.
                        view_motion.step(*world->objects.at(*world->player).pose,double(world->simulation_ticks()),[&]{
                            const bool surface_slow=dok::player_surface(*world);
                            movement.slow=movement.slow||surface_slow;
                            dok::update_player_motion(*world,movement,static_cast<std::uint32_t>(world->simulation_ticks()),settings.inertia);
                        });
                    }else if(event==dok::GameEvent::weapon_tick||event==dok::GameEvent::weapon_switch)weapons->event(owner,event);
                    else if(event==dok::GameEvent::projectile_tick)projectiles->event(owner);
                    else if(event==dok::GameEvent::monster_tick)monsters->event(owner);
                    else dok::door_event(*world,owner,event);
                });
                const auto phase=flow->observe();if(phase!=dok::SessionPhase::playing){if(phase==dok::SessionPhase::completed)menu.ending();else menu.outcome(phase);input.capture_mouse(false);input.clear();use_pending=false;}
            }
            if(video_changed)view_motion.reset();
            display->begin();auto* gl=display->gl();const auto output=display->size();
            if(menu.active()){
                const auto& frame=menu.render(output[0],output[1]);display->pixels(frame.pixels,frame.width,frame.height,false);
            }else if(world){
                const auto pose=*world->objects.at(*world->player).pose;
                if(gl){
                    gl->set_linear_filter(settings.linear);
                    const auto bounds=dok::view_cell_bounds(pose,world->definition.cell_side,world->definition.width,world->definition.length,gl->view_width());
                    auto dynamic=monsters->images(bounds);const auto shots=projectiles->images();dynamic.insert(dynamic.end(),shots.begin(),shots.end());
                    gl->world(scene->palette());scene->render(*world,pose,dynamic,[&](const auto& vertices,const auto& image,dok::Bytes light,dok::RasterAxis axis){gl->polygon(vertices,image,light,axis);},gl->view_width(),view_motion.offset(world->presentation_ticks()));
                    dok::IndexedSurface overlay(gl->view_width(),160);weapons->overlay(overlay);std::vector<std::uint32_t> pixels(overlay.pixels.size());
                    for(std::size_t i=0;i<pixels.size();++i)if(overlay.pixels[i])pixels[i]=scene->palette()[overlay.pixels[i]];
                    gl->image(pixels,overlay.width(),overlay.height(),{0,0,320,240});
                    const int hud_scale=std::max(1,output[1]/720);auto frame=hud->desktop(std::max(320,output[0]/hud_scale),*world->objects.at(*world->player).player_state,scene->palette(),world->visible_message(),world->presentation_ticks()*119.0/dok::GameClock::pit_hz);
                    gl->image(frame.pixels,frame.width,frame.height,{0,output[1]-frame.height*hud_scale,frame.width*hud_scale,frame.height*hud_scale},true);
                }else{
                    const auto mode=dok::software_modes[settings.video.software_mode];scene->software_resolution(mode[0],mode[1]);
                    const auto bounds=dok::original_cell_bounds(pose,world->definition.cell_side,world->definition.width,world->definition.length);
                    auto dynamic=monsters->images(bounds);const auto shots=projectiles->images();dynamic.insert(dynamic.end(),shots.begin(),shots.end());
                    auto frame=scene->render(*world,pose,dynamic);weapons->overlay(frame,mode[1]);frame=hud->classic_mode(frame,mode[1],*world->objects.at(*world->player).player_state,world->visible_message());
                    scene_pixels.resize(frame.pixels.size());std::transform(frame.pixels.begin(),frame.pixels.end(),scene_pixels.begin(),[&](auto c){return scene->palette()[c];});display->pixels(scene_pixels,frame.width(),frame.height(),true,settings.linear);
                }
            }
            ++frames;++fps_frames;const auto fps_now=SDL_GetPerformanceCounter();const auto seconds=double(fps_now-fps_time)/SDL_GetPerformanceFrequency();
            if(seconds>=0.5){fps_value=unsigned(fps_frames/seconds+0.5);fps_frames=0;fps_time=fps_now;}
            if(settings.show_fps&&world&&!menu.active()){
                const std::string text="FPS "+(fps_value?std::to_string(fps_value):"--");const auto font=sets[current].archive.font("FONT8");
                const int w=int(text.size())*8+4;std::vector<std::uint32_t> pixels(w*12,0xff101010);
                for(std::size_t c=0;c<text.size();++c)for(int y=0;y<8;++y)for(int x=0;x<8;++x)if(font.data[static_cast<unsigned char>(text[c])*8+y]&(0x80>>x))pixels[(y+2)*w+int(c)*8+x+2]=0xfff0f0f0;
                display->overlay(pixels,w,12,{output[0]-w*2-4,4,w*2,24});
            }
            if(frame_limit&&frames==frame_limit&&!screenshot.empty())captures.push_back(screenshot);
            if(capture_requested){try{auto dir=std::filesystem::current_path()/"screenshots";std::filesystem::create_directories(dir);
                const auto stamp=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();captures.push_back((dir/("screenshot-"+std::to_string(stamp)+"-"+std::to_string(frames)+".png")).u8string());}catch(const std::exception& e){log<<"Screenshot failed: "<<e.what()<<'\n';}}
            for(const auto& capture:captures)try{display->screenshot(capture);log<<"Screenshot saved: "<<capture<<'\n';}catch(const std::exception& e){log<<"Screenshot failed: "<<e.what()<<'\n';}
            display->present();if(frame_limit&&frames>=frame_limit)running=false;SDL_Delay(1);
        }
        log<<"Clean shutdown; frames="<<frames<<" datasets="<<sets.size()<<" menu="<<int(menu.screen())<<'\n';
        if(world){const auto& pose=*world->objects.at(*world->player).pose;log<<"Player x="<<pose.x<<" z="<<pose.z<<" heading="<<pose.heading<<" ticks="<<world->simulation_ticks()<<'\n';}
        std::cout<<"SDL completed: "<<frames<<" frames, "<<sets.size()<<" datasets\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
