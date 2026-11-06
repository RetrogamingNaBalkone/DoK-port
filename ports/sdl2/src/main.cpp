#include "dok/resources.hpp"
#include "dok/scene.hpp"
#include "dok/movement.hpp"
#include "dok/sdl_input.hpp"
#include "dok/interactions.hpp"
#include "dok/items.hpp"
#include "dok/weapons.hpp"
#include "dok/monsters.hpp"
#include "dok/animations.hpp"
#include "dok/flow.hpp"
#include "dok/hud.hpp"
#include "dok/sdl_audio.hpp"
#include "dok/display.hpp"
#include "dok/presentation.hpp"
#include "dok/frontend.hpp"
#include "dok/saves.hpp"
#include "dok/localization.hpp"
#include "dok/view_motion.hpp"
#include "dok/cheat.hpp"
#include "dok/launch.hpp"
#include "dok/input_replay.hpp"
#include <chrono>
#include <cmath>
#include <cctype>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#endif

namespace {
struct SdlEnd {~SdlEnd(){SDL_Quit();}};
struct Loaded {std::filesystem::path path;dok::Archive archive;};
struct ScriptEvent {unsigned frame;std::string action,value;};
}
int main(int argc,char** argv){
#ifdef _WIN32
    // The MinGW CRT supplies ANSI argv. Paths throughout the port use UTF-8.
    std::vector<std::string> utf8_arguments;std::vector<char*> utf8_argv;
    int wide_count=0;wchar_t** wide=CommandLineToArgvW(GetCommandLineW(),&wide_count);
    if(!wide)return 1;
    for(int i=0;i<wide_count;++i){
        const int size=WideCharToMultiByte(CP_UTF8,0,wide[i],-1,nullptr,0,nullptr,nullptr);
        if(size<=0){LocalFree(wide);return 1;}
        std::string value(size,'\0');WideCharToMultiByte(CP_UTF8,0,wide[i],-1,value.data(),size,nullptr,nullptr);
        value.pop_back();utf8_arguments.push_back(std::move(value));
    }
    LocalFree(wide);for(auto& value:utf8_arguments)utf8_argv.push_back(value.data());utf8_argv.push_back(nullptr);
    argc=wide_count;argv=utf8_argv.data();
#endif
    const bool diagnostic_hidden=std::any_of(argv+1,argv+argc,[](const char* arg){return std::string(arg)=="--hidden";});
    std::filesystem::path log_path;
    const auto fail=[&](const char* message){
        std::cerr<<message<<'\n';
        if(!log_path.empty()){std::ofstream log(log_path,std::ios::app);log<<"Fatal error: "<<message<<'\n';}
#ifdef _WIN32
        if(!diagnostic_hidden)SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"OGDoK",message,nullptr);
#endif
        return 1;
    };
    SDL_SetMainReady();
    // ALLOW_HIGHDPI alone does not declare Windows DPI awareness. Request it
    // before video initialization so Windows never bitmap-scales our glyphs.
    // SDL falls back to the available awareness level on older Windows.
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS,"permonitorv2");
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_SCALING,"0"); // Selected sizes are pixels.
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER|SDL_INIT_AUDIO)!=0)return fail(SDL_GetError());SdlEnd sdl;
    try{
        const auto arguments=dok::parse_launch_arguments(argc,argv);
        dok::AppSettings settings;
        unsigned frame_limit=0;std::string screenshot,level_name,script_path,replay_path;std::optional<dok::InputReplay> replay;std::vector<std::string> paths,source_choices;bool skip_intro=false,replay_live_input=false,hidden=false;
        const auto apply_arguments=[&]{
        paths.clear();source_choices.clear();
        for(const auto& option:arguments.options){const auto& arg=option.first;
            auto value=[&]()->std::string{return option.second;};
            if(arg=="--res")paths.push_back(value());else if(arg=="--frames")frame_limit=std::stoul(value());
            else if(arg=="--input-replay")replay_path=value();
            else if(arg=="--hidden")hidden=true;
            else if(arg=="--replay-live-input")replay_live_input=true;
            else if(arg=="--res-source")source_choices.push_back(value());
            else if(arg=="--screenshot")screenshot=value();else if(arg=="--level")level_name=value();else if(arg=="--skip-intro")skip_intro=true;
            else if(arg=="--user-dir")value();else if(arg=="--ui-script")script_path=value();
            else if(arg=="--renderer"){const auto mode=value();if(mode!="gl"&&mode!="software")throw std::runtime_error("Renderer: gl|software");settings.video.renderer=mode=="gl"?dok::RendererKind::opengl:dok::RendererKind::software;}
            else if(arg=="--filter"){const auto mode=value();const std::array<std::string,8> modes{"nearest","smooth","linear","mipmaps","trilinear","aniso2","aniso4","aniso8"};auto it=std::find(modes.begin(),modes.end(),mode);if(it==modes.end())throw std::runtime_error("Filter: nearest|smooth");settings.texture_filter(mode=="nearest"?0:6);}
            else if(arg=="--frame-filter"){const auto mode=value();if(mode!="nearest"&&mode!="linear"&&mode!="sharp"&&mode!="xbr")throw std::runtime_error("Frame filter: nearest|linear|sharp|xbr");settings.frame_filter=mode=="xbr"?3:mode=="sharp"?2:mode=="linear"?1:0;}
            else if(arg=="--fxaa"){const auto mode=value();if(mode!="on"&&mode!="off")throw std::runtime_error("FXAA: on|off");settings.fxaa=mode=="on";}
            else if(arg=="--hud"){const auto mode=value();if(mode!="classic"&&mode!="desktop"&&mode!="auto")throw std::runtime_error("HUD: auto|classic|desktop");settings.auto_hud=mode=="auto";settings.classic_hud=mode=="classic";}
            else if(arg=="--aspect"||arg=="--fov")throw std::runtime_error("FOV and aspect are automatic; select a resolution with --size instead");
            else if(arg=="--locale"){const auto lang=value();if(lang!="ru"&&lang!="en")throw std::runtime_error("Locale: ru|en");settings.locale=lang=="ru"?dok::LocaleId::ru:dok::LocaleId::en;}
            else if(arg=="--size"||arg=="--software-mode"){
                const auto size=value();const auto x=size.find('x');if(x==std::string::npos)throw std::runtime_error("Size: WIDTHxHEIGHT");
                const int w=std::stoi(size.substr(0,x)),h=std::stoi(size.substr(x+1));if(w<320||h<200||w>7680||h>4320)throw std::runtime_error("Invalid window size");
                if(arg=="--size"){settings.video.width=w;settings.video.height=h;settings.video.fullscreen_width=w;settings.video.fullscreen_height=h;}
                else{int index=-1;for(int n=0;n<int(dok::software_modes.size());++n)if(dok::software_modes[n]==std::array<int,2>{w,h})index=n;
                    if(index<0)throw std::runtime_error("Unsupported original software mode");settings.video.software_mode=index;}
            }else throw std::runtime_error("Unknown option: "+arg);
        }
        };
        // Validate option values before touching a profile, then apply the same
        // parsed overrides to the loaded settings without persisting them.
        apply_arguments();
        char* base=SDL_GetBasePath();if(!base)throw std::runtime_error(SDL_GetError());
        const auto executable_path=std::filesystem::absolute(std::filesystem::u8path(base)).lexically_normal();SDL_free(base);
        const auto profile=dok::select_profile(arguments,executable_path,[]{
            char* pref=SDL_GetPrefPath("DoK-port","SourcePort");if(!pref)throw std::runtime_error(SDL_GetError());
            const auto path=std::filesystem::u8path(pref);SDL_free(pref);return path;
        });
        const auto& user_path=profile.first;const auto& profile_mode=profile.second;
        std::filesystem::create_directories(user_path);log_path=user_path/"port.log";std::ofstream log(log_path);
        if(!log)throw std::runtime_error("Cannot write profile log: "+log_path.u8string());
        log<<"Profile mode: "<<profile_mode<<"\nProfile directory: "<<user_path.u8string()<<'\n';
        std::string warning;auto stored=dok::load_settings(user_path/"settings.ini",warning);
        bool settings_backup_required=!warning.empty();
        for(auto& path:stored.resource_paths)path=dok::resolve_resource_path(path,user_path).u8string();
        for(auto& path:stored.resource_choices)if(!path.empty())path=dok::resolve_resource_path(path,user_path).u8string();
        settings=stored;apply_arguments();
        if(replay_live_input&&replay_path.empty())throw std::runtime_error("--replay-live-input requires --input-replay");
        if(!replay_path.empty()){
            if(!skip_intro||level_name.empty())throw std::runtime_error("Input replay requires --level and --skip-intro");
            std::ifstream file(std::filesystem::u8path(replay_path));if(!file)throw std::runtime_error("Cannot read input replay");
            replay=dok::InputReplay::read(file);if(replay->level!=level_name)throw std::runtime_error("Input replay level mismatch");
        }
        std::vector<ScriptEvent> script;
        if(!script_path.empty()){std::ifstream f(std::filesystem::u8path(script_path));if(!f)throw std::runtime_error("Cannot read UI script");std::string line;
            while(std::getline(f,line)){if(line.empty()||line[0]=='#')continue;std::istringstream in(line);ScriptEvent event{};in>>event.frame>>event.action;std::getline(in>>std::ws,event.value);script.push_back(event);}}
        std::unique_ptr<dok::Display> display;
        try{display=std::make_unique<dok::Display>(settings.video,!replay&&!hidden);}catch(const std::exception& e){
            if(settings.video.renderer!=dok::RendererKind::opengl)throw;log<<"OpenGL fallback: "<<e.what()<<'\n';settings.video.renderer=dok::RendererKind::software;
            display=std::make_unique<dok::Display>(settings.video,!replay&&!hidden);warning="OpenGL unavailable; software renderer selected.";
        }
        if(!replay&&!hidden)SDL_ShowWindow(display->window());dok::SdlInput input(settings.input);dok::Frontend menu(settings);
        if(hidden)log<<"Diagnostic hidden presentation: no shown window, VSync or mouse capture; natural input/world timing retained\n";
        if(replay)log<<"Replay presentation: hidden window, VSync off; ordinary world callbacks and every rendered frame retained\n";
        log<<"Audio driver: "<<(SDL_GetCurrentAudioDriver()?SDL_GetCurrentAudioDriver():"unavailable")<<'\n';
        std::vector<Loaded> sets;dok::ResourceSources sources;std::string resource_errors;
        auto refresh_sets=[&]{
            sets.clear();std::string errors=resource_errors;
            for(const auto id:{dok::DatasetId::old_gold,dok::DatasetId::kremlin_pkr,dok::DatasetId::kremlin_pke}){
                if(const auto selected=sources.selected(id)){const auto& source=sources.sources()[*selected];sets.push_back({source.path,source.archive});}
                else{
                    bool heading=false;
                    for(const auto& source:sources.sources())if(source.archive.identify()==id){
                        if(!heading){errors+=std::string(dok::dataset_name(id))+": choose a source with --res-source PATH\n";heading=true;}
                        errors+=source.path.u8string()+"\n";
                    }
                }
            }
            std::vector<dok::FrontendDataset> items;for(auto& set:sets)items.push_back({&set.archive,set.path.filename().u8string()});menu.datasets(std::move(items),errors);menu.resources(sources);
        };
        auto load=[&](const std::string& path,dok::DatasetId expected=dok::DatasetId::unknown){bool admitted=false;try{
            auto a=dok::Archive::open(std::filesystem::u8path(path));if(a.identify()==dok::DatasetId::unknown)throw std::runtime_error("Unknown RES catalog");
            if(expected!=dok::DatasetId::unknown&&a.identify()!=expected)throw std::runtime_error("RES belongs to another game or edition");
            // Validate all level references before admitting a candidate. One bad
            // file must not invalidate a previously usable source or other games.
            for(const auto& entry:a.entries())if(entry.name.rfind("LEVEL",0)==0)dok::load_level(a,entry.name);
            const auto dataset=a.identify();const auto count=sources.sources().size();sources.add(std::filesystem::u8path(path),std::move(a));admitted=true;
            log<<(sources.sources().size()==count?"Duplicate ignored ":"Loaded ")<<dok::dataset_name(dataset)<<": "<<path<<'\n';
            if(!sources.selected(dataset))log<<"Source selection required: "<<dok::dataset_name(dataset)<<'\n';
        }catch(const std::exception& e){resource_errors+=std::filesystem::u8path(path).filename().u8string()+": "+e.what()+"\n";log<<resource_errors;}refresh_sets();return admitted;};
        const auto forget_missing_resources=[&]{
            const auto missing=[](const std::string& path){std::error_code error;const bool exists=std::filesystem::exists(std::filesystem::u8path(path),error);return !error&&!exists;};
            auto& paths=settings.resource_paths;paths.erase(std::remove_if(paths.begin(),paths.end(),missing),paths.end());
            for(auto& path:settings.resource_choices)if(!path.empty()&&missing(path))path.clear();
        };
        if(!arguments.explicit_resources){
            for(const auto& entry:std::filesystem::directory_iterator(executable_path)){
                std::error_code error;
                if(dok::ascii_lower(entry.path().extension().u8string())==".res"&&entry.is_regular_file(error))paths.push_back(entry.path().u8string());
            }
            std::sort(paths.begin(),paths.end());
            for(const auto& path:stored.resource_paths)if(std::find(paths.begin(),paths.end(),path)==paths.end())paths.push_back(path);
        }
        for(auto& path:paths)path=std::filesystem::absolute(std::filesystem::u8path(path)).lexically_normal().u8string();
        for(const auto& path:paths)load(path);
        for(unsigned id=1;id<4;++id)if(!arguments.explicit_resources&&!stored.resource_choices[id].empty()){
            const auto chosen=sources.find_source(std::filesystem::u8path(stored.resource_choices[id]));
            bool known=true;for(const auto& candidate:sources.sources())if(candidate.archive.identify()==dok::DatasetId(id)&&std::find(stored.resource_paths.begin(),stored.resource_paths.end(),candidate.path.u8string())==stored.resource_paths.end())known=false;
            if(known&&chosen&&sources.sources()[*chosen].archive.identify()==dok::DatasetId(id))sources.select(dok::DatasetId(id),*chosen);
        }
        for(const auto& path:source_choices){
            const auto chosen=sources.find_source(std::filesystem::u8path(path));
            if(!chosen)throw std::runtime_error("--res-source must name an admitted --res source: "+path);
            const auto& source=sources.sources()[*chosen];sources.select(source.archive.identify(),*chosen);log<<"Selected source: "<<source.path.u8string()<<'\n';
        }
        refresh_sets();
        std::size_t current=menu.dataset_index();
        std::unique_ptr<dok::World> world;std::unique_ptr<dok::SoftwareScene> scene;std::unique_ptr<dok::Weapons> weapons;
        std::unique_ptr<dok::Projectiles> projectiles;std::unique_ptr<dok::Monsters> monsters;std::unique_ptr<dok::GameFlow> flow;std::unique_ptr<dok::Animations> animations;
        std::unique_ptr<dok::Hud> hud;std::unique_ptr<dok::SdlAudio> audio,music;
        dok::ViewMotion view_motion;dok::CheatSequence cheat;std::vector<std::uint32_t> scene_pixels;
        dok::FirePressLatch fire_press;
        std::uint64_t discovery_ticks=~std::uint64_t(0);
        std::array<std::int32_t,4> discovery_pose{};
        auto map_visible=[&]{return world&&menu.screen()==dok::MenuScreen::map;};
        auto menu_blocks=[&]{return menu.active()&&!map_visible();};
        bool running=true,use_pending=false,use_previous=false;
        auto clear_world=[&]{menu.session_world(nullptr);audio.reset();hud.reset();flow.reset();monsters.reset();projectiles.reset();weapons.reset();animations.reset();scene.reset();world.reset();discovery_ticks=~std::uint64_t(0);view_motion.reset();cheat.reset();input.clear();fire_press.clear();input.capture_mouse(false);};
        auto start=[&](const std::string& level){
            clear_world();music.reset();current=menu.dataset_index();const auto& a=sets.at(current).archive;level_name=level;
            if(replay&&!replay->finished()){const auto id=a.identify();const std::string dataset=id==dok::DatasetId::old_gold?"old_gold":id==dok::DatasetId::kremlin_pkr?"kremlin_pkr":"kremlin_pke";
                if(replay->dataset!=dataset||replay->level!=level)throw std::runtime_error("Input replay dataset/level mismatch");}
            world=std::make_unique<dok::World>(dok::load_level(a,level));weapons=std::make_unique<dok::Weapons>(*world,a);
            projectiles=std::make_unique<dok::Projectiles>(*world,a);monsters=std::make_unique<dok::Monsters>(*world,a,*projectiles);
            animations=std::make_unique<dok::Animations>(*world,a);
            flow=std::make_unique<dok::GameFlow>(*world);hud=std::make_unique<dok::Hud>(a);scene=std::make_unique<dok::SoftwareScene>(a,world->definition);
            try{audio=std::make_unique<dok::SdlAudio>(a);world->audio=[&](const dok::AudioCommand& c){audio->command(c);};}catch(const std::exception& e){log<<"Audio unavailable: "<<e.what()<<'\n';}
            menu.session_world(world.get());menu.hide();input.clear();fire_press.clear();input.capture_mouse(!replay&&!hidden);use_pending=use_previous=false;log<<"Started "<<dok::dataset_name(a.identify())<<' '<<level<<'\n';
        };
        if(replay){
            bool found=false;for(std::size_t i=0;i<sets.size();++i){const auto id=sets[i].archive.identify();
                const std::string dataset=id==dok::DatasetId::old_gold?"old_gold":id==dok::DatasetId::kremlin_pkr?"kremlin_pkr":"kremlin_pke";
                if(dataset==replay->dataset){menu.select_dataset(i);found=true;break;}}
            if(!found)throw std::runtime_error("Input replay requires an available matching dataset");
        }
        if(!level_name.empty()&&!sets.empty()){
            if(level_name=="LEVEL4")for(std::size_t i=0;i<sets.size();++i)if(sets[i].archive.identify()==dok::DatasetId::old_gold)menu.select_dataset(i);
            if(level_name.size()!=6||level_name.substr(0,5)!="LEVEL"||level_name[5]<'1'||level_name[5]>'4')throw std::runtime_error("Invalid level");
            menu.preview_level(level_name[5]-'0');if(skip_intro)start(level_name);
        }
        if(!warning.empty()){log<<"Settings/display notice: "<<warning<<'\n';menu.notice("Настройки восстановлены или OpenGL недоступен. Подробности в port.log.","Settings restored or OpenGL unavailable. See port.log for details.");}
        struct PendingSave {int slot;std::string name;};
        std::optional<PendingSave> pending_save;
        bool reset_frame_clock=false;
        dok::SaveMetadata cached_preview;std::uint64_t preview_ticks=~std::uint64_t(0);
        menu.save_catalog=[&]{if(sets.empty())return std::vector<dok::SaveEntry>{};return dok::list_saves(user_path,sets.at(menu.dataset_index()).archive);};
        auto request_save=[&](int slot,const std::string& name){if(!settings.saves_enabled)return;if(!menu.save_allowed()){menu.notice("Сохранение доступно только при живом герое в незавершённом уровне.","Saving requires a living player in an unfinished level.");return;}pending_save=PendingSave{slot,name};};
        auto load_game=[&](int slot,bool backup){if(!settings.saves_enabled)return false;
            try{
                const auto next_index=world?current:menu.dataset_index();const auto& a=sets.at(next_index).archive;auto path=dok::save_path(user_path,a.identify(),slot);if(backup)path+=".bak";
                auto saved=dok::decode_save(dok::read_save_file(path),a);auto next_world=std::move(saved.world);
                auto next_weapons=std::make_unique<dok::Weapons>(*next_world,a);
                auto next_shots=std::make_unique<dok::Projectiles>(*next_world,a);
                auto next_monsters=std::make_unique<dok::Monsters>(*next_world,a,*next_shots);
                auto next_animations=std::make_unique<dok::Animations>(*next_world,a);
                auto next_flow=std::make_unique<dok::GameFlow>(*next_world);next_flow->restore(saved.flow);
                auto next_hud=std::make_unique<dok::Hud>(a);auto next_scene=std::make_unique<dok::SoftwareScene>(a,next_world->definition);
                std::unique_ptr<dok::SdlAudio> created_audio;auto* candidate_audio=audio?audio.get():music.get();
                if(!candidate_audio)try{created_audio=std::make_unique<dok::SdlAudio>(a);candidate_audio=created_audio.get();}catch(const std::exception& error){log<<"Audio unavailable: "<<error.what()<<'\n';}
                dok::SdlAudioSnapshot audio_state;audio_state.voice=saved.audio.voice;audio_state.resource=saved.audio.resource;audio_state.volume=settings.effects_volume;audio_state.paused=true;
                if(candidate_audio&&!candidate_audio->can_restore(audio_state))throw std::runtime_error("Cannot restore sound state");
                const auto next_level=saved.metadata.level;
                // Reuse the device: some drivers allow only one open output. All
                // fallible validation is finished before detaching the live device.
                auto next_audio=audio?std::move(audio):music?std::move(music):std::move(created_audio);
                if(next_audio)next_audio->restore(audio_state);
                clear_world();music.reset();current=next_index;level_name=next_level;
                world=std::move(next_world);weapons=std::move(next_weapons);projectiles=std::move(next_shots);monsters=std::move(next_monsters);animations=std::move(next_animations);flow=std::move(next_flow);hud=std::move(next_hud);scene=std::move(next_scene);audio=std::move(next_audio);
                if(audio){world->audio=[target=audio.get()](const dok::AudioCommand& c){target->command(c);};audio->volume(settings.effects_volume);audio->pause(false);}
                menu.select_dataset(current);menu.preview_level(level_name.back()-'0');menu.session_world(world.get());menu.hide();input.clear();fire_press.clear();use_pending=use_previous=false;view_motion.reset();cheat.reset();input.capture_mouse(!replay&&!hidden);preview_ticks=~std::uint64_t(0);cached_preview={};reset_frame_clock=true;world->notify(dok::cp866(dok::game_text("port.loaded",settings.locale)));world->message_key="port.loaded";
                log<<"SAVE loaded slot="<<slot<<" backup="<<backup<<" dataset="<<int(a.identify())<<" level="<<level_name<<" ticks="<<world->simulation_ticks()<<'\n';return true;
            }catch(const std::exception& error){log<<"SAVE load failed: "<<error.what()<<'\n';menu.notice("Не удалось загрузить сохранение. Текущая игра сохранена. Подробности в port.log.","Could not load save. The current session is preserved. See port.log.");return false;}
        };
        auto quickload=[&]{
            if(!settings.saves_enabled||sets.empty())return false;const auto index=world?current:menu.dataset_index();const auto entries=dok::list_saves(user_path,sets[index].archive);const auto& entry=entries.front();
            if(!entry.exists){menu.notice("Быстрого сохранения пока нет.","No quicksave yet.");return false;}
            if(entry.backup){menu.recover_quicksave();return false;}return load_game(0,false);
        };
        std::string save_error;
        auto persist=[&]{
            try{
                if(settings_backup_required){
                    const auto file=user_path/"settings.ini";auto backup=user_path/"settings.ini.bak";
                    for(unsigned n=1;std::filesystem::exists(backup);++n)backup=user_path/("settings.ini.bak."+std::to_string(n));
                    std::filesystem::copy_file(file,backup);settings_backup_required=false;
                    log<<"Original settings preserved: "<<backup.u8string()<<'\n';
                }
                auto encoded=stored;
                for(auto& path:encoded.resource_paths)path=dok::store_resource_path(path,user_path);
                for(auto& path:encoded.resource_choices)if(!path.empty())path=dok::store_resource_path(path,user_path);
                if(!dok::save_settings(user_path/"settings.ini",encoded,save_error))throw std::runtime_error(save_error);
            }catch(const std::exception& error){log<<"Settings save failed: "<<error.what()<<'\n';menu.notice("Не удалось сохранить настройки. Проверьте доступ к папке профиля.","Could not save settings. Check the profile directory.");}
        };
        if(!std::filesystem::exists(user_path/"settings.ini"))persist();
        unsigned frames=0,fps_frames=0,fps_value=0;auto previous_time=SDL_GetPerformanceCounter(),fps_time=previous_time;
        unsigned drawn_fps=0;dok::DatasetId fps_dataset=dok::DatasetId::unknown;
        std::vector<std::uint32_t> fps_pixels;int fps_width=0;
        while(running){
            const auto before=settings;bool settings_dirty=false,flow_changed=false,capture_requested=false,map_closed=false;
            bool video_changed=false;std::vector<std::string> captures;menu.update(SDL_GetTicks64());
            for(const auto& event:script)if(event.frame==frames){
                if(event.action=="state"){
                    const auto size=display->size();log<<"STATE "<<event.value<<" renderer="<<int(settings.video.renderer)<<" size="<<size[0]<<'x'<<size[1]<<" menu="<<int(menu.screen())<<" frame_filter="<<settings.frame_filter<<" fxaa="<<settings.fxaa<<" classic_hud="<<settings.classic_hud<<" auto_hud="<<settings.auto_hud<<" automatic_projection="<<true<<" gpu_presentation="<<display->gpu_presentation()<<" gpu_xbr="<<display->gpu_xbr_ready();
                    if(world){const auto& p=*world->objects.at(*world->player).pose;const auto& player=*world->objects.at(*world->player).player_state;
                        std::uint64_t hash=14695981039346656037ULL;for(const auto& map:world->maps)for(auto byte:map.second){hash^=static_cast<unsigned char>(byte);hash*=1099511628211ULL;}
                        log<<" world="<<world.get()<<" ticks="<<world->simulation_ticks()<<" pose="<<p.x<<','<<p.y<<','<<p.z<<','<<p.heading<<" hp="<<player.health()<<" charge="<<player.charge()<<" maps="<<hash;
                        log<<" lives="<<player.lives<<" keys="<<player.keys()<<" score="<<player.score<<" phase="<<int(flow->phase());
                        log<<" weapon="<<player.current_weapon<<" pending="<<player.pending_weapon<<" shots="<<projectiles->active();
                        if(const auto owner=player.weapons.at(player.current_weapon)){const auto& gun=*world->object_for_owner(owner).weapon;
                            log<<" gun_state="<<gun.state<<" gun_frame="<<gun.frame<<" gun_shift="<<gun.shift<<" gun_events="<<world->events.events(owner).size();}
                        log<<" locale="<<int(settings.locale)<<" message_key="<<world->message_key<<" message_hex=";
                        for(unsigned char c:world->visible_message(settings.locale)){const char* digits="0123456789abcdef";log<<digits[c>>4]<<digits[c&15];}
                    }log<<'\n';
                }
                else if(event.action=="live_input"&&replay)replay_live_input=event.value=="on";
                else if(event.action=="text"){std::size_t at=0;while(at<event.value.size()){auto count=std::min<std::size_t>(31,event.value.size()-at);while(count&&at+count<event.value.size()&&(static_cast<unsigned char>(event.value[at+count])&0xc0)==0x80)--count;if(!count)break;SDL_Event e{};e.type=SDL_TEXTINPUT;std::copy_n(event.value.data()+at,count,e.text.text);e.text.text[count]=0;SDL_PushEvent(&e);at+=count;}}
                else if(event.action=="shot")captures.push_back(event.value);
                else if(event.action=="keydown"||event.action=="keyup"){SDL_Event e{};e.type=event.action=="keydown"?SDL_KEYDOWN:SDL_KEYUP;e.key.keysym.scancode=SDL_GetScancodeFromName(event.value.c_str());e.key.keysym.sym=SDL_GetKeyFromScancode(e.key.keysym.scancode);SDL_PushEvent(&e);}
                else if(event.action=="mouse_down"||event.action=="mouse_up"){SDL_Event e{};e.type=event.action=="mouse_down"?SDL_MOUSEBUTTONDOWN:SDL_MOUSEBUTTONUP;e.button.button=static_cast<Uint8>(std::stoul(event.value));SDL_PushEvent(&e);}
                else if(event.action=="wheel"){SDL_Event e{};e.type=SDL_MOUSEWHEEL;e.wheel.y=std::stoi(event.value);SDL_PushEvent(&e);}
                else if(event.action=="key"){SDL_Event e{};e.type=SDL_KEYDOWN;e.key.keysym.scancode=SDL_GetScancodeFromName(event.value.c_str());e.key.keysym.sym=SDL_GetKeyFromScancode(e.key.keysym.scancode);SDL_PushEvent(&e);e.type=SDL_KEYUP;SDL_PushEvent(&e);}
                else if(event.action=="pad"){SDL_Event e{};e.type=SDL_CONTROLLERBUTTONDOWN;e.cbutton.button=std::stoi(event.value);SDL_PushEvent(&e);e.type=SDL_CONTROLLERBUTTONUP;SDL_PushEvent(&e);}
                else if(event.action=="drop"){SDL_Event e{};e.type=SDL_DROPFILE;e.drop.file=SDL_strdup(event.value.c_str());if(SDL_PushEvent(&e)!=1)SDL_free(e.drop.file);}
                else if(event.action=="quit"){SDL_Event e{};e.type=SDL_QUIT;SDL_PushEvent(&e);}
            }
            SDL_Event e;
            while(SDL_PollEvent(&e)){
                if(e.type==SDL_WINDOWEVENT&&e.window.windowID!=SDL_GetWindowID(display->window()))continue;
                input.handle(e);
                if(e.type==SDL_WINDOWEVENT&&e.window.event==SDL_WINDOWEVENT_FOCUS_LOST)fire_press.clear();
                if(e.type==SDL_CONTROLLERDEVICEREMOVED)fire_press.clear();
                if(e.type==SDL_QUIT){if(world){menu.pause();menu.request_quit();}else running=false;}
                if(!replay&&!hidden&&e.type==SDL_WINDOWEVENT&&e.window.event==SDL_WINDOWEVENT_FOCUS_LOST){input.clear();fire_press.clear();cheat.reset();if(world&&!menu_blocks())menu.pause();input.capture_mouse(false);}
                if(e.type==SDL_DROPFILE){const std::string path=e.drop.file;SDL_free(e.drop.file);
                    if(world){log<<"Resource load deferred: active world\n";menu.notice("Добавьте ресурсы из главного меню.","Add game resources from the main menu.");}else{music.reset();if(load(path)){forget_missing_resources();if(const auto found=sources.find_source(std::filesystem::u8path(path))){const auto id=sources.sources()[*found].archive.identify();if(!sources.selected(id))settings.resource_choices[static_cast<unsigned>(id)].clear();if(std::find(settings.resource_paths.begin(),settings.resource_paths.end(),path)==settings.resource_paths.end())settings.resource_paths.push_back(path);}}}continue;}
                const bool was_menu=menu.active(),was_map=map_visible();
                if(e.type==SDL_KEYDOWN&&!e.key.repeat&&!menu.capturing()){
                    if(e.key.keysym.sym==SDLK_F12)capture_requested=true;
                    if(e.key.keysym.sym==SDLK_F3)settings.show_fps=!settings.show_fps;
                    if(e.key.keysym.sym==SDLK_F4){if(settings.video.renderer==dok::RendererKind::opengl)settings.texture_filter(settings.texture_filter()?0:6);else settings.frame_filter=(settings.frame_filter+1)%4;}
                    if(e.key.keysym.sym==SDLK_F11&&!menu.video_pending()){
                        const bool next=!settings.video.fullscreen;
                        auto video=settings.video;video.fullscreen=next;
                        if(next)SDL_GetWindowSize(display->window(),&video.width,&video.height);
                        try{auto replacement=std::make_unique<dok::Display>(video,!replay&&!hidden);display=std::move(replacement);settings.video=video;stored.video.fullscreen=next;settings_dirty=true;menu.window_mode_changed(next);video_changed=true;if(!replay&&!hidden)SDL_ShowWindow(display->window());}
                        catch(const std::exception& error){log<<"Fullscreen switch rejected: "<<error.what()<<'\n';}
                    }
                }
                if(was_menu&&!was_map&&(menu.screen()==dok::MenuScreen::pause||menu.screen()==dok::MenuScreen::outcome)){
                    const auto menu_actions=input.sample();
                    if(menu_actions.quickload){quickload();flow_changed=true;input.clear();continue;}
                    if(menu_actions.quicksave&&menu.screen()==dok::MenuScreen::pause){request_save(0,settings.locale==dok::LocaleId::ru?"Быстрое сохранение":"Quicksave");flow_changed=true;input.clear();continue;}
                }
                if(was_menu&&!was_map){auto mapped=e;const auto size=display->size();if(display->menu_event(mapped))menu.handle(mapped,size[0],size[1]);input.clear();fire_press.clear();use_pending=false;cheat.reset();flow_changed=true;}
                else {
                    if(was_map){auto mapped=e;const auto size=display->size();if(display->menu_event(mapped))menu.handle(mapped,size[0],size[1]);map_closed|=!map_visible();}
                    if(!map_closed&&e.type==SDL_KEYDOWN&&!e.key.repeat){
                    if(e.key.keysym.sym==SDLK_ESCAPE){menu.pause();input.clear();fire_press.clear();input.capture_mouse(false);flow_changed=true;}
                    else if(world&&!(e.key.keysym.mod&(KMOD_CTRL|KMOD_ALT|KMOD_GUI))){const auto key=e.key.keysym.scancode;
                        if(key>=SDL_SCANCODE_A&&key<=SDL_SCANCODE_Z&&cheat.push(char('A'+key-SDL_SCANCODE_A))){weapons->grant_all();log<<"IDKFA\n";}}
                    }
                }
            }
            if(input.bindings.mouse_enabled!=settings.input.mouse_enabled||input.bindings.gamepad_enabled!=settings.input.gamepad_enabled){fire_press.clear();use_pending=use_previous=false;}
            input.configure(settings.input);
            for(const auto command:menu.commands()){
                flow_changed|=!(map_closed&&command==dok::MenuCommand::resume);
                switch(command){
                case dok::MenuCommand::save_game:request_save(menu.pending_save_slot(),menu.pending_save_name());break;
                case dok::MenuCommand::load_game:load_game(menu.pending_save_slot(),menu.pending_save_backup());break;
                case dok::MenuCommand::delete_game:try{dok::delete_save(dok::save_path(user_path,sets.at(menu.dataset_index()).archive.identify(),menu.pending_save_slot()));menu.refresh_saves();}catch(const std::exception& error){log<<"SAVE delete failed: "<<error.what()<<'\n';menu.notice("Не удалось удалить сохранение.","Could not delete save.");}break;
                case dok::MenuCommand::quicksave:request_save(0,settings.locale==dok::LocaleId::ru?"Быстрое сохранение":"Quicksave");break;
                case dok::MenuCommand::quickload:quickload();break;
                case dok::MenuCommand::start:try{start(menu.level()==4?"LEVEL4":"LEVEL"+std::to_string(menu.level()));}catch(const std::exception& e){clear_world();menu.main_menu();menu.notice("Не удалось загрузить уровень. Подробности в port.log.","Could not load level. See port.log.");log<<e.what()<<'\n';}break;
                case dok::MenuCommand::add_resource:
                    if(!world){music.reset();const auto path=menu.pending_resource();
                        if(!load(path,menu.resource_target())){menu.notice(dok::menu_text(dok::MenuString::resource_load_failed,dok::LocaleId::ru),dok::menu_text(dok::MenuString::resource_load_failed,dok::LocaleId::en));break;}
                        forget_missing_resources();
                        if(const auto found=sources.find_source(std::filesystem::u8path(path))){
                            const auto& source=sources.sources()[*found];const auto id=source.archive.identify();sources.select(id,*found);
                            settings.resource_choices[static_cast<unsigned>(id)]=source.path.u8string();
                            for(const auto& candidate:sources.sources())if(std::find(settings.resource_paths.begin(),settings.resource_paths.end(),candidate.path.u8string())==settings.resource_paths.end())settings.resource_paths.push_back(candidate.path.u8string());
                            refresh_sets();
                        }
                    }break;
                case dok::MenuCommand::select_source:
                    if(!world){music.reset();const auto index=menu.pending_source();const auto& source=sources.sources().at(index);const auto id=source.archive.identify();
                        forget_missing_resources();
                        sources.select(id,index);settings.resource_choices[static_cast<unsigned>(id)]=source.path.u8string();
                        for(const auto& candidate:sources.sources())if(std::find(settings.resource_paths.begin(),settings.resource_paths.end(),candidate.path.u8string())==settings.resource_paths.end())settings.resource_paths.push_back(candidate.path.u8string());
                        refresh_sets();
                    }break;
                case dok::MenuCommand::resume:if(!map_closed){input.clear();fire_press.clear();}input.capture_mouse(!replay&&!hidden);break;
                case dok::MenuCommand::main:clear_world();break;
                case dok::MenuCommand::quit:running=false;break;
                case dok::MenuCommand::retry:
                    if(flow&&flow->retry()){view_motion.reset();input.clear();fire_press.clear();input.capture_mouse(!replay&&!hidden);}
                    else if(flow){menu.outcome(flow->phase());input.clear();fire_press.clear();input.capture_mouse(false);}
                    break;
                case dok::MenuCommand::music_stop:music.reset();break;
                case dok::MenuCommand::music_title:case dok::MenuCommand::music_story:
                    music.reset();if(!sets.empty())try{music=std::make_unique<dok::SdlAudio>(sets[menu.dataset_index()].archive);music->volume(settings.music_volume);music->command({sets[menu.dataset_index()].archive.identify()==dok::DatasetId::old_gold?dok::AudioAction::loop:dok::AudioAction::once,1,dok::cp866("ЗАСТАВКА_ЗВУК")});const auto music_state=music->snapshot();log<<"MUSIC dataset="<<int(sets[menu.dataset_index()].archive.identify())<<" loop="<<music_state.voice.loop<<" active="<<music_state.voice.id<<'\n';}catch(const std::exception& e){log<<"Music unavailable: "<<e.what()<<'\n';}break;
                case dok::MenuCommand::apply_video:
                    try{auto next=std::make_unique<dok::Display>(settings.video,!replay&&!hidden);input.capture_mouse(false);display=std::move(next);if(!replay&&!hidden)SDL_ShowWindow(display->window());video_changed=true;}
                    catch(const std::exception& e){log<<"Video rejected: "<<e.what()<<'\n';menu.video_failed(e.what());}break;
                case dok::MenuCommand::keep_video:{auto old=settings;old.video=menu.previous_video();settings_dirty|=dok::merge_settings_edits(stored,old,settings);break;}
                case dok::MenuCommand::revert_video:
                    try{auto next=std::make_unique<dok::Display>(settings.video,!replay&&!hidden);display=std::move(next);if(!replay&&!hidden)SDL_ShowWindow(display->window());video_changed=true;}
                    catch(const std::exception& e){log<<"Video rollback failed: "<<e.what()<<'\n';settings.video.renderer=dok::RendererKind::software;settings.video.fullscreen=false;display=std::make_unique<dok::Display>(settings.video,!replay&&!hidden);if(!replay&&!hidden)SDL_ShowWindow(display->window());menu.notice("Возврат видеорежима не удался. Включён software.","Video rollback failed. Software renderer enabled.");}break;
                }
            }
            settings_dirty|=dok::merge_settings_edits(stored,before,settings,false);
            if(settings_dirty)persist();
            if(audio){audio->volume(settings.effects_volume);audio->pause(menu_blocks());}
            if(music)music->volume(settings.music_volume);
            const auto now=SDL_GetPerformanceCounter();const auto elapsed_ns=replay?16666667ULL:static_cast<std::uint64_t>(double(now-previous_time)*1e9/SDL_GetPerformanceFrequency());previous_time=now;
            const auto actions=input.sample();
            if(world&&!menu_blocks()&&actions.quickload){quickload();flow_changed=true;}
            else if(world&&!menu_blocks()&&actions.quicksave){request_save(0,settings.locale==dok::LocaleId::ru?"Быстрое сохранение":"Quicksave");flow_changed=true;}

            if(reset_frame_clock){previous_time=SDL_GetPerformanceCounter();reset_frame_clock=false;}
            if(world&&!menu.active()&&actions.map&&!map_closed)menu.show_map();
            if(world&&!menu_blocks()&&actions.pause&&!map_closed){menu.pause();input.clear();fire_press.clear();input.capture_mouse(false);flow_changed=true;}
            if((!replay||replay_live_input)&&actions.use&&!use_previous)use_pending=true;use_previous=actions.use;
            if(menu_blocks()){use_pending=false;fire_press.clear();input.capture_mouse(false);view_motion.reset();}
            if(world&&!menu_blocks()&&!flow_changed&&flow->phase()==dok::SessionPhase::playing&&(!replay||replay_live_input))fire_press.observe(actions);
            else fire_press.clear();
            if(world&&!menu_blocks()&&!flow_changed&&flow->phase()==dok::SessionPhase::playing){
                if(!replay||replay_live_input){
                    // Live 1/2 and either wheel direction all request the other owned weapon.
                    if(actions.select_weapon>=0||actions.next_weapon||actions.previous_weapon)weapons->cycle(actions.previous_weapon?-1:1);
                    dok::apply_mouse_yaw(*world,actions.yaw_delta);
                }
                world->advance_events(elapsed_ns,[&](dok::OwnerId owner,dok::CallbackId callback){
                    const auto event=static_cast<dok::GameEvent>(callback);
                    if(event==dok::GameEvent::player_tick){
                        if(world->objects.at(*world->player).player_state->health()<=0||world->level_complete)return;
                        auto tick_actions=actions;
                        if(replay){
                            if(!replay_live_input)tick_actions={};
                            if(!replay->finished()){
                                const auto sample=replay->take(world->simulation_ticks());
                                if(!replay_live_input){tick_actions=sample.actions;
                                    const auto heading=world->objects.at(*world->player).pose->heading;
                                    dok::apply_mouse_yaw(*world,float(heading-sample.heading));}
                            }
                            if(!replay_live_input){
                                if(tick_actions.select_weapon>=0)weapons->select(tick_actions.select_weapon);
                                if(tick_actions.next_weapon)weapons->cycle(1);if(tick_actions.previous_weapon)weapons->cycle(-1);
                                if(tick_actions.use)use_pending=true;}
                        }
                        // Gameplay activation uses the original simulation region;
                        // backend, window size and frame timing must not widen it.
                        monsters->consider(dok::original_cell_bounds(*world->objects.at(*world->player).pose,
                            world->definition.cell_side,world->definition.width,world->definition.length));
                        animations->consider(dok::original_cell_bounds(*world->objects.at(*world->player).pose,
                            world->definition.cell_side,world->definition.width,world->definition.length));
                        const bool fire=(!replay||replay_live_input)?fire_press.take(tick_actions.fire):tick_actions.fire;
                        if(fire)weapons->fire([&](std::uint8_t glyph){return projectiles->spawn(owner,glyph);});
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
                        auto movement=tick_actions;
                        // Interpolate the entire player displacement, including currents.
                        view_motion.step(*world->objects.at(*world->player).pose,double(world->simulation_ticks()),[&]{
                            const bool surface_slow=dok::player_surface(*world);
                            movement.slow=movement.slow||surface_slow;
                            dok::update_player_motion(*world,movement,static_cast<std::uint32_t>(world->simulation_ticks()),replay?replay->inertia:settings.inertia);
                        });
                    }else if(event==dok::GameEvent::weapon_tick||event==dok::GameEvent::weapon_switch)weapons->event(owner,event);
                    else if(event==dok::GameEvent::projectile_tick)projectiles->event(owner);
                    else if(event==dok::GameEvent::monster_tick)monsters->event(owner);
                    else if(event==dok::GameEvent::base_animation)animations->event(owner);
                    else dok::door_event(*world,owner,event);
                });
                const auto phase=flow->observe();if(replay&&!frame_limit&&phase!=dok::SessionPhase::playing&&!replay->finished())throw std::runtime_error("Input replay interrupted by game outcome before its final sample");if(phase!=dok::SessionPhase::playing){if(phase==dok::SessionPhase::completed)menu.ending();else menu.outcome(phase);input.capture_mouse(false);input.clear();fire_press.clear();use_pending=false;}
            }
            if(video_changed)view_motion.reset();
            if(world&&!menu_blocks()&&flow->phase()==dok::SessionPhase::playing){
                const auto p=*world->objects.at(*world->player).pose;
                const std::array<std::int32_t,4> key{{p.x,p.y,p.z,p.heading}};
                if(discovery_ticks!=world->simulation_ticks()||discovery_pose!=key){
                    const auto bounds=dok::original_cell_bounds(p,world->definition.cell_side,world->definition.width,world->definition.length);
                    auto dynamic=monsters->images(bounds);const auto shots=projectiles->images();dynamic.insert(dynamic.end(),shots.begin(),shots.end());
                    for(const auto cell:scene->discovered_cells(*world,p,dynamic,[&](auto owner){return animations->image(owner);}))world->visited.at(cell)=1;
                    discovery_ticks=world->simulation_ticks();discovery_pose=key;
                }
                if(map_visible()){
                    const int x=std::clamp(p.x/world->definition.cell_side,0,world->definition.width-1),z=std::clamp(p.z/world->definition.cell_side,0,world->definition.length-1);
                    world->visited.at(z*world->definition.width+x)=1;
                }
            }
            const bool capture_preview=world&&(pending_save.has_value()||(menu_blocks()&&preview_ticks!=world->simulation_ticks()));
            display->begin();auto* gl=display->gl();const auto output=display->size();
            SDL_Rect gameplay_rect{0,0,output[0],output[1]};
            const bool full_map=map_visible();
            if((menu_blocks()||full_map)&&!capture_preview){
                const auto& frame=menu.render(output[0],output[1]);
                display->pixels(frame.pixels,frame.width,frame.height,false);
            }else if(world){
                const auto view=dok::player_view(*world,settings.camera_bobbing,capture_preview?std::array<double,4>{}:view_motion.offset(world->presentation_ticks()));
                const auto pose=view.pose;
                const auto layout=dok::presentation_layout(output[0],output[1],settings.classic_hud,settings.auto_hud,world->definition.dataset==dok::DatasetId::old_gold,!gl);
                const auto rect=[](dok::PresentationRect r){return SDL_Rect{r.x,r.y,r.w,r.h};};
                const auto frame_rect=rect(layout.frame),scene_rect=rect(layout.scene);
                gameplay_rect=frame_rect;
                const int view_width=layout.view_width,scene_height=layout.view_height;
                const auto bounds=dok::view_cell_bounds(pose,world->definition.cell_side,world->definition.width,world->definition.length,view_width);
                auto dynamic=monsters->images(bounds);const auto shots=projectiles->images();dynamic.insert(dynamic.end(),shots.begin(),shots.end());
                if(gl){
                    gl->set_texture_filter(settings.texture_filter());
                    gl->scene_view(scene_rect,view_width,scene_height);
                    gl->world(scene->palette(),true,true);
                    scene->render(*world,pose,dynamic,[&](const auto& vertices,const auto& image,dok::Bytes light,dok::RasterAxis axis){gl->polygon(vertices,image,light,axis);},view_width,view.offset,[&](auto owner){return animations->image(owner);},scene_height);
                    gl->finish_scene(settings.fxaa);
                }else{
                    const auto mode=dok::software_modes[settings.video.software_mode];
                    const int h=std::max(2,(mode[1]*scene_height/200)/2*2);
                    const int w=std::max(1,int(std::lround(double(h)*view_width/scene_height)));
                    scene->software_presentation(w,h,scene_height);
                    const auto& frame=scene->render(*world,pose,dynamic,{},320,{},[&](auto owner){return animations->image(owner);});
                    scene_pixels.resize(frame.pixels.size());
                    std::transform(frame.pixels.begin(),frame.pixels.end(),scene_pixels.begin(),[&](auto c){return scene->palette()[c];});
                    display->pixels(scene_pixels,frame.width(),frame.height(),false,settings.frame_filter,&scene_rect);
                }
                // Weapons use a fixed central DOS canvas, independent of the expanded world.
                dok::IndexedSurface weapon(320,scene_height);weapons->overlay(weapon);
                std::vector<std::uint32_t> pixels(weapon.pixels.size());
                for(std::size_t i=0;i<pixels.size();++i)if(weapon.pixels[i])pixels[i]=scene->palette()[weapon.pixels[i]];
                display->overlay(pixels,weapon.width(),weapon.height(),rect(layout.weapon));
                if(layout.classic){
                    const auto backdrop=hud->classic_backdrop(view_width);
                    pixels.resize(backdrop.pixels.size());
                    std::transform(backdrop.pixels.begin(),backdrop.pixels.end(),pixels.begin(),[&](auto c){return scene->palette()[c];});
                    display->overlay(pixels,backdrop.width(),backdrop.height(),{frame_rect.x,layout.panel.y,frame_rect.w,layout.panel.h});
                    const int native_height=world->definition.dataset==dok::DatasetId::old_gold?170:160;
                    const auto panel=hud->classic(dok::IndexedSurface(320,native_height),*world->objects.at(*world->player).player_state,world->visible_message(settings.locale));
                    pixels.resize(320*(200-native_height));
                    for(std::size_t i=0;i<pixels.size();++i)pixels[i]=scene->palette()[panel.pixels[native_height*320+i]];
                    display->overlay(pixels,320,200-native_height,rect(layout.panel));
                }else{
                    const int hud_scale=std::max(1,frame_rect.h/720);
                    const auto& frame=hud->desktop(std::max(320,frame_rect.w/hud_scale),*world->objects.at(*world->player).player_state,scene->palette(),world->visible_message(settings.locale),world->presentation_ticks()*119.0/dok::GameClock::pit_hz);
                    display->overlay(frame.pixels,frame.width,frame.height,{frame_rect.x,frame_rect.y+frame_rect.h-frame.height*hud_scale,frame.width*hud_scale,frame.height*hud_scale});
                }
            }
            if(capture_preview){
                try{const auto pixels=display->read_pixels();const auto size=display->size();cached_preview.preview_width=320;cached_preview.preview_height=200;cached_preview.preview.assign(320*200,0xff000000);
                    const double scale=std::min(320.0/size[0],200.0/size[1]);const int pw=std::max(1,int(size[0]*scale)),ph=std::max(1,int(size[1]*scale));const int ox=(320-pw)/2,oy=(200-ph)/2;
                    for(int y=0;y<ph;++y)for(int x=0;x<pw;++x)cached_preview.preview[(oy+y)*320+ox+x]=pixels[std::size_t(y)*size[1]/ph*size[0]+std::size_t(x)*size[0]/pw];preview_ticks=world->simulation_ticks();
                }catch(const std::exception& error){log<<"SAVE preview failed: "<<error.what()<<'\n';pending_save.reset();menu.notice("Не удалось получить превью сохранения.","Could not capture the save preview.");}
            }
            if(!settings.saves_enabled)pending_save.reset();
            if(pending_save){
                const auto request=*pending_save;pending_save.reset();try{auto metadata=cached_preview;metadata.name=request.name;metadata.timestamp=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();metadata.fingerprint=dok::save_fingerprint(sets[current].archive,level_name);
                    dok::SaveAudio sound;if(audio){const auto state=audio->snapshot();sound.voice=state.voice;sound.resource=state.resource;}
                    auto bytes=dok::encode_save(*world,flow->snapshot(),sound,std::move(metadata));
                    dok::write_save_file(dok::save_path(user_path,world->definition.dataset,request.slot),bytes,sets[current].archive);menu.refresh_saves();log<<"SAVE written slot="<<request.slot<<" level="<<level_name<<" ticks="<<world->simulation_ticks()<<'\n';world->notify(dok::cp866(dok::game_text("port.saved",settings.locale)));world->message_key="port.saved";
                }catch(const std::exception& error){log<<"SAVE write failed: "<<error.what()<<'\n';menu.notice("Не удалось сохранить игру. Предыдущая запись сохранена. Проверьте доступ к папке профиля; подробности в port.log.","Could not save game. The previous save is preserved. Check the profile directory; see port.log.");}
            }
            if(capture_preview)previous_time=SDL_GetPerformanceCounter();
            if(capture_preview&&(menu_blocks()||full_map)){const auto& frame=menu.render(output[0],output[1]);display->pixels(frame.pixels,frame.width,frame.height,false);}
            ++frames;++fps_frames;const auto fps_now=SDL_GetPerformanceCounter();const auto seconds=double(fps_now-fps_time)/SDL_GetPerformanceFrequency();
            if(seconds>=0.5){fps_value=unsigned(fps_frames/seconds+0.5);fps_frames=0;fps_time=fps_now;}
            if(settings.show_fps&&world&&!menu.active()){
                if(fps_pixels.empty()||drawn_fps!=fps_value||fps_dataset!=world->definition.dataset){
                const std::string text="FPS "+(fps_value?std::to_string(fps_value):"--");const auto font=sets[current].archive.font("FONT8");
                const int w=int(text.size())*8+4;fps_pixels.assign(w*12,0xff101010);auto& pixels=fps_pixels;
                for(std::size_t c=0;c<text.size();++c)for(int y=0;y<8;++y)for(int x=0;x<8;++x)if(font.data[static_cast<unsigned char>(text[c])*8+y]&(0x80>>x))pixels[(y+2)*w+int(c)*8+x+2]=0xfff0f0f0;
                fps_width=w;drawn_fps=fps_value;fps_dataset=world->definition.dataset;
                }
                display->overlay(fps_pixels,fps_width,12,{output[0]-fps_width*2-4,4,fps_width*2,24});
            }
            if(!screenshot.empty()&&((frame_limit&&frames==frame_limit)||(!frame_limit&&replay&&replay->finished())))captures.push_back(screenshot);
            if(capture_requested){try{auto dir=user_path/"screenshots";std::filesystem::create_directories(dir);
                const auto stamp=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();captures.push_back((dir/("screenshot-"+std::to_string(stamp)+"-"+std::to_string(frames)+".png")).u8string());}catch(const std::exception& e){log<<"Screenshot failed: "<<e.what()<<'\n';}}
            for(const auto& capture:captures)try{display->screenshot(capture);log<<"Screenshot saved: "<<capture<<'\n';}catch(const std::exception& e){log<<"Screenshot failed: "<<e.what()<<'\n';}
            display->present();if((frame_limit&&frames>=frame_limit)||(!frame_limit&&replay&&replay->finished()))running=false;SDL_Delay(1);
        }
        if(replay&&world){const auto& actor=world->objects.at(*world->player);const auto& p=*actor.pose;const auto& player=*actor.player_state;
            log<<"REPLAY ticks="<<world->simulation_ticks()<<" pose="<<p.x<<','<<p.y<<','<<p.z<<','<<p.heading
               <<" hp="<<player.health()<<" charge="<<player.charge()<<" score="<<player.score<<" current="<<player.current_weapon<<" weapons=";
            log<<player.weapons[1]<<','<<player.weapons[2]<<" shots="<<projectiles->active()<<'\n';
        }
        log<<"Clean shutdown; frames="<<frames<<" datasets="<<sets.size()<<" menu="<<int(menu.screen())<<'\n';
        if(world){const auto& pose=*world->objects.at(*world->player).pose;log<<"Player x="<<pose.x<<" z="<<pose.z<<" heading="<<pose.heading<<" ticks="<<world->simulation_ticks()<<'\n';}
        std::cout<<"SDL completed: "<<frames<<" frames, "<<sets.size()<<" datasets\n";return 0;
    }catch(const std::exception& e){return fail(e.what());}
}
