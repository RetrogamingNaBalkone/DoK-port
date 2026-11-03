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
#include "dok/text.hpp"
#include "dok/sdl_audio.hpp"
#include "dok/gl_renderer.hpp"
#include "dok/png.hpp"
#include "dok/view_motion.hpp"
#include "dok/cheat.hpp"
#include <chrono>
#include <SDL.h>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
struct SdlEnd { ~SdlEnd(){SDL_Quit();} };
template<class T, void(*Destroy)(T*)> using SdlPtr=std::unique_ptr<T,decltype(Destroy)>;
struct Loaded {std::filesystem::path path; dok::Archive archive;};
}
int main(int argc,char** argv) {
    SDL_SetMainReady();
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER|SDL_INIT_AUDIO)!=0){std::cerr<<SDL_GetError()<<'\n';return 1;}
    SdlEnd end;
    try {
        unsigned frame_limit=0;std::string screenshot,level_name;std::vector<std::string> paths;bool skip_intro=false,use_gl=true,linear_filter=false;int window_width=1280,window_height=720;
        for(int i=1;i<argc;++i) {
            std::string arg=argv[i];
            if(arg=="--res" && i+1<argc) paths.push_back(argv[++i]);
            else if(arg=="--frames" && i+1<argc) frame_limit=static_cast<unsigned>(std::stoul(argv[++i]));
            else if(arg=="--screenshot" && i+1<argc) screenshot=argv[++i];
            else if(arg=="--level" && i+1<argc) level_name=argv[++i];
            else if(arg=="--skip-intro")skip_intro=true;
            else if(arg=="--filter"&&i+1<argc){const std::string filter=argv[++i];if(filter!="nearest"&&filter!="linear")throw std::runtime_error("Filter must be nearest or linear");linear_filter=filter=="linear";}
            else if(arg=="--renderer"&&i+1<argc){const std::string mode=argv[++i];if(mode!="gl"&&mode!="software")throw std::runtime_error("Renderer must be gl or software");use_gl=mode=="gl";}
            else if(arg=="--size"&&i+1<argc){const std::string size=argv[++i];const auto x=size.find('x');if(x==std::string::npos)throw std::runtime_error("Size must be WIDTHxHEIGHT");window_width=std::stoi(size.substr(0,x));window_height=std::stoi(size.substr(x+1));if(window_width<320||window_height<240)throw std::runtime_error("Window must be at least320x240");}
            else throw std::runtime_error("Usage: dok_port [--res FILE]... [--level LEVEL1..4] [--renderer gl|software] [--filter nearest|linear] [--size WIDTHxHEIGHT] [--skip-intro] [--frames N] [--screenshot FILE.bmp|FILE.png]");
        }
        char* pref=SDL_GetPrefPath("DoK-port","SourcePort");
        if(!pref)throw std::runtime_error(SDL_GetError());
        auto user_path=std::filesystem::u8path(pref); SDL_free(pref);
        std::ofstream log(user_path/"port.log");
        log<<"SDL2 bootstrap; user directory: "<<user_path.u8string()<<'\n';
        if(use_gl){SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,2);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,0);SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,24);}
        SdlPtr<SDL_Window,SDL_DestroyWindow> window(SDL_CreateWindow("DoK Source Port",
            SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,window_width,window_height,SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI|(use_gl?SDL_WINDOW_OPENGL:0)),SDL_DestroyWindow);
        if(!window) throw std::runtime_error(SDL_GetError());
        std::unique_ptr<dok::GlRenderer> gl;
        SdlPtr<SDL_Renderer,SDL_DestroyRenderer> renderer(nullptr,SDL_DestroyRenderer);
        if(use_gl)gl=std::make_unique<dok::GlRenderer>(window.get());
        else{renderer.reset(SDL_CreateRenderer(window.get(),-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC));
            if(!renderer)renderer.reset(SDL_CreateRenderer(window.get(),-1,SDL_RENDERER_SOFTWARE));if(!renderer)throw std::runtime_error(SDL_GetError());}
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"0");
        if(renderer)SDL_RenderSetLogicalSize(renderer.get(),320,240);
        SdlPtr<SDL_Texture,SDL_DestroyTexture> texture(nullptr,SDL_DestroyTexture);
        std::vector<Loaded> sets;
        auto load=[&](const std::string& path) {
            try {
                auto a=dok::Archive::open(std::filesystem::u8path(path));
                if(a.identify()==dok::DatasetId::unknown)throw std::runtime_error("Unrecognized RES catalog: "+path);
                for(const auto& s:sets)if(s.archive.identify()==a.identify())throw std::runtime_error("Dataset already loaded: "+path);
                log<<"Loaded "<<dok::dataset_name(a.identify())<<": "<<path<<'\n';
                sets.push_back({std::filesystem::u8path(path),std::move(a)});
            }catch(const std::exception& e){log<<e.what()<<'\n';std::cerr<<e.what()<<'\n';SDL_SetWindowTitle(window.get(),e.what());}
        };
        if(paths.empty()){
            char* base=SDL_GetBasePath();
            if(base){const auto directory=std::filesystem::u8path(base);SDL_free(base);
                for(const auto* name:{"PKR.RES","PKE.RES","OLD_GOLD.RES"})if(std::filesystem::exists(directory/name))paths.push_back((directory/name).u8string());
            }
        }
        for(const auto& path:paths)load(path);
        std::size_t current=0;
        int menu_level=1;
        int image_width=0,image_height=0;
        std::unique_ptr<dok::World> world;
        std::unique_ptr<dok::SoftwareScene> scene;
        std::unique_ptr<dok::Weapons> weapons;
        std::unique_ptr<dok::Projectiles> projectiles;
        std::unique_ptr<dok::Monsters> monsters;
        std::unique_ptr<dok::GameFlow> flow;
        std::unique_ptr<dok::Hud> hud;
        std::unique_ptr<dok::SdlAudio> audio;
        std::vector<dok::TextPage> story;
        std::size_t story_page=0;
        bool ending=false;
        dok::SdlInput input;
        bool paused=false;
        dok::ViewMotion view_motion;
        dok::CheatSequence cheat;
        std::vector<std::uint32_t> scene_pixels;
        std::vector<std::uint32_t> title_pixels,ui_pixels(320*240);
        auto point=[&](int x,int y,std::uint32_t color){if(x<0||y<0||x>=320||y>=240)return;
            if(gl)ui_pixels[y*320+x]=color;else{SDL_SetRenderDrawColor(renderer.get(),color>>16,color>>8,color,color>>24);SDL_RenderDrawPoint(renderer.get(),x,y);}};
        auto panel=[&](SDL_Rect r){if(gl){for(int y=r.y;y<r.y+r.h;++y)for(int x=r.x;x<r.x+r.w;++x)point(x,y,0xff0c0e14);}
            else{SDL_SetRenderDrawColor(renderer.get(),12,14,20,255);SDL_RenderFillRect(renderer.get(),&r);}};
        auto line=[&](const std::string& text,int y,int x=-1){
            if(sets.empty()||!sets[current].archive.find("FONT8"))return;
            const auto font=sets[current].archive.font("FONT8");
            const int left=x>=0?x:std::max(0,(320-static_cast<int>(text.size())*8)/2);
            for(std::size_t c=0;c<text.size();++c)for(int row=0;row<8;++row){
                const auto bits=font.data[static_cast<unsigned char>(text[c])*8+row];
                for(int bit=0;bit<8;++bit)if(bits&(0x80>>bit))point(left+static_cast<int>(c)*8+bit,y+row,0xfff0f0f0);
            }
        };
        auto draw_scene=[&] {
            if(gl)return;
            const auto pose=*world->objects.at(*world->player).pose;
            const auto bounds=dok::original_cell_bounds(pose,world->definition.cell_side,world->definition.width,world->definition.length);
            auto instances=monsters->images(bounds);const auto shots=projectiles->images();instances.insert(instances.end(),shots.begin(),shots.end());
            auto frame=scene->render(*world,pose,instances);
            weapons->overlay(frame);
            frame=hud->classic(frame,*world->objects.at(*world->player).player_state,world->visible_message());
            scene_pixels.resize(frame.pixels.size());
            std::transform(frame.pixels.begin(),frame.pixels.end(),scene_pixels.begin(),[&](auto i){return scene->palette()[i];});
            if(SDL_UpdateTexture(texture.get(),nullptr,scene_pixels.data(),frame.width()*4)!=0)throw std::runtime_error(SDL_GetError());
        };
        auto show=[&] {
            audio.reset();view_motion.reset();cheat.reset();
            story.clear();story_page=0;ending=false;
            texture.reset();hud.reset();flow.reset();monsters.reset();projectiles.reset();weapons.reset();scene.reset();world.reset();if(sets.empty())return;
            const auto& a=sets[current].archive;
            if(!level_name.empty()){
                world=std::make_unique<dok::World>(dok::load_level(a,level_name));
                weapons=std::make_unique<dok::Weapons>(*world,a);
                projectiles=std::make_unique<dok::Projectiles>(*world,a);
                monsters=std::make_unique<dok::Monsters>(*world,a,*projectiles);
                flow=std::make_unique<dok::GameFlow>(*world);
                hud=std::make_unique<dok::Hud>(a);
                try{audio=std::make_unique<dok::SdlAudio>(a);
                    world->audio=[&](const dok::AudioCommand& c){audio->command(c);};
                }catch(const std::exception& e){log<<"Audio unavailable: "<<e.what()<<'\n';}
                scene=std::make_unique<dok::SoftwareScene>(a,world->definition);
                image_width=320;image_height=200;
                if(!gl){texture.reset(SDL_CreateTexture(renderer.get(),SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,image_width,image_height));
                    if(!texture)throw std::runtime_error(SDL_GetError());}
                draw_scene();
                paused=false;
                if(!skip_intro)story=dok::paginate_story(a.get(a.identify()==dok::DatasetId::old_gold?"INTRO4B":"BINTRO"+level_name.substr(5)));
                if(!input.capture_mouse(story.empty()))log<<"Mouse capture: "<<SDL_GetError()<<'\n';
                log<<"Scene "<<level_name<<": "<<scene->polygon_count()<<" surfaces\n";
                auto title=std::string("DoK - ")+dok::dataset_name(a.identify())+" | "+level_name+" | WASD + mouse/arrows | Esc: pause | F11: fullscreen";
                SDL_SetWindowTitle(window.get(),title.c_str());
                return;
            }
            // Temporary resource presentation, not the SP07 game renderer.
            const std::string title_image="\x87\x80\x91\x92\x80\x82\x8a\x80_\x91\x8f\x90";
            const std::string title_palette="\x87\x80\x91\x92\x80\x82\x8a\x80_\x8f\x80\x8b";
            std::string image_name=a.find(title_image)?title_image:"INTRO4BSPR";
            auto img=a.image(image_name);
            std::string palette_name;
            for(const auto& r:a.entries())if(r.size==768){palette_name=r.name;break;}
            if(a.find(title_palette))palette_name=title_palette;
            auto pal=a.palette(palette_name);
            std::vector<std::uint32_t> pixels(img.pixels.size());
            std::transform(img.pixels.begin(),img.pixels.end(),pixels.begin(),[&](auto i){return pal[i];});
            title_pixels=pixels;
            if(!gl){texture.reset(SDL_CreateTexture(renderer.get(),SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STATIC,img.width,img.height));
                if(!texture || SDL_UpdateTexture(texture.get(),nullptr,pixels.data(),img.width*4)!=0)throw std::runtime_error(SDL_GetError());}
            image_width=static_cast<int>(img.width);image_height=static_cast<int>(img.height);
            std::string title=std::string("DoK - ")+dok::dataset_name(a.identify())+" | 1/2/3: level | Enter/A: LEVEL1 | Left/Right: dataset | Esc: exit";
            SDL_SetWindowTitle(window.get(),title.c_str());
        };
        show();
        bool running=true,fullscreen=false;unsigned frames=0;
        bool use_previous=false,use_pending=false;
        auto previous_time=SDL_GetPerformanceCounter();
        auto fps_time=SDL_GetPerformanceCounter();
        unsigned fps_frames=0,fps_value=0;
        bool show_fps=true;
        while(running) {
            bool flow_changed=false;
            bool capture_requested=false;
            SDL_Event e;
            while(SDL_PollEvent(&e)) {
                input.handle(e);
                if(!world||paused||!story.empty()||flow->phase()!=dok::SessionPhase::playing)cheat.reset();
                else if(e.type==SDL_KEYDOWN&&!e.key.repeat&&!(e.key.keysym.mod&(KMOD_CTRL|KMOD_ALT|KMOD_GUI))){
                    const auto key=e.key.keysym.scancode;
                    if(key>=SDL_SCANCODE_A&&key<=SDL_SCANCODE_Z&&cheat.push(char('A'+key-SDL_SCANCODE_A))){
                        weapons->grant_all();log<<"IDKFA\n";
                    }
                }
                if(e.type==SDL_WINDOWEVENT&&e.window.event==SDL_WINDOWEVENT_FOCUS_LOST)cheat.reset();
                if(e.type==SDL_QUIT)running=false;
                if(e.type==SDL_WINDOWEVENT&&e.window.event==SDL_WINDOWEVENT_FOCUS_LOST&&world){paused=true;input.capture_mouse(false);}
                if(e.type==SDL_DROPFILE){std::string path=e.drop.file;SDL_free(e.drop.file);load(path);if(!world)show();}
                int direction=0;
                bool confirm=false;int selected_level=0;
                const bool cancel=(e.type==SDL_KEYDOWN&&!e.key.repeat&&e.key.keysym.sym==SDLK_ESCAPE)||
                    (e.type==SDL_CONTROLLERBUTTONDOWN&&e.cbutton.button==SDL_CONTROLLER_BUTTON_B);
                if(cancel&&!story.empty()){level_name.clear();show();input.capture_mouse(false);flow_changed=true;}
                if(e.type==SDL_KEYDOWN && !e.key.repeat) {
                    confirm=e.key.keysym.sym==SDLK_RETURN||e.key.keysym.sym==SDLK_SPACE;
                    if(e.key.keysym.sym>=SDLK_1&&e.key.keysym.sym<=SDLK_3)selected_level=e.key.keysym.sym-SDLK_1+1;
                    if(!world&&e.key.keysym.sym==SDLK_UP)menu_level=(menu_level+1)%3+1;
                    if(!world&&e.key.keysym.sym==SDLK_DOWN)menu_level=menu_level%3+1;
                    if(e.key.keysym.sym==SDLK_ESCAPE&&!world)running=false;
                    if(e.key.keysym.sym==SDLK_LEFT)direction=-1;
                    if(e.key.keysym.sym==SDLK_RIGHT)direction=1;
                    if(e.key.keysym.sym==SDLK_F11){fullscreen=!fullscreen;SDL_SetWindowFullscreen(window.get(),fullscreen?SDL_WINDOW_FULLSCREEN_DESKTOP:0);}
                    if(e.key.keysym.sym==SDLK_F3)show_fps=!show_fps;
                    if(e.key.keysym.sym==SDLK_F4){linear_filter=!linear_filter;log<<"Texture filter: "<<(linear_filter?"linear":"nearest")<<'\n';}
                    if(e.key.keysym.sym==SDLK_F12)capture_requested=true;
                }
                if(e.type==SDL_CONTROLLERBUTTONDOWN){
                    confirm=e.cbutton.button==SDL_CONTROLLER_BUTTON_A||e.cbutton.button==SDL_CONTROLLER_BUTTON_START;
                    if(!world&&e.cbutton.button==SDL_CONTROLLER_BUTTON_DPAD_UP)menu_level=(menu_level+1)%3+1;
                    if(!world&&e.cbutton.button==SDL_CONTROLLER_BUTTON_DPAD_DOWN)menu_level=menu_level%3+1;
                    if(e.cbutton.button==SDL_CONTROLLER_BUTTON_DPAD_LEFT)direction=-1;
                    if(e.cbutton.button==SDL_CONTROLLER_BUTTON_DPAD_RIGHT)direction=1;
                    if(e.cbutton.button==SDL_CONTROLLER_BUTTON_BACK)running=false;
                }
                if(direction && !world && !sets.empty()){current=(current+sets.size()+direction)%sets.size();show();}
                if(!flow_changed&&!story.empty()&&confirm){
                    if(++story_page>=story.size()){
                        story.clear();story_page=0;
                        if(ending){level_name.clear();show();input.capture_mouse(false);}
                        else input.capture_mouse(true);
                    }
                    flow_changed=true;use_pending=false;
                }else if(!flow_changed&&world&&flow->phase()!=dok::SessionPhase::playing&&confirm){
                    if(flow->retry()){paused=false;input.capture_mouse(true);draw_scene();SDL_SetWindowTitle(window.get(),("DoK - "+level_name).c_str());}
                    else{level_name.clear();show();input.capture_mouse(false);}
                    flow_changed=true;use_pending=false;
                }else if(!flow_changed&&!world&&!sets.empty()&&(confirm||selected_level)){
                    if(selected_level)menu_level=selected_level;
                    level_name=sets[current].archive.identify()==dok::DatasetId::old_gold?"LEVEL4":"LEVEL"+std::to_string(menu_level);
                    show();flow_changed=true;use_pending=false;
                }
            }
            const auto now=SDL_GetPerformanceCounter();
            const auto elapsed_ns=static_cast<std::uint64_t>(double(now-previous_time)*1e9/SDL_GetPerformanceFrequency());previous_time=now;
            const auto actions=input.sample();
            bool pause_changed=false;
            if(world&&story.empty()&&!flow_changed&&flow->phase()==dok::SessionPhase::playing&&actions.pause){paused=!paused;pause_changed=true;input.capture_mouse(!paused);}
            if(actions.use&&!use_previous)use_pending=true;
            use_previous=actions.use;
            if(paused||pause_changed){use_pending=false;cheat.reset();}
            if(audio)audio->pause(paused||!story.empty());
            if(world&&story.empty()&&!paused&&!pause_changed&&!flow_changed&&flow->phase()==dok::SessionPhase::playing){
                if(actions.select_weapon>=0)weapons->select(actions.select_weapon);
                if(actions.next_weapon)weapons->cycle(1);
                if(actions.previous_weapon)weapons->cycle(-1);
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
                        const bool surface_slow=dok::player_surface(*world);
                        movement.slow=movement.slow||surface_slow;
                        const auto before=*world->objects.at(*world->player).pose;
                        dok::update_player_motion(*world,movement,static_cast<std::uint32_t>(world->simulation_ticks()));
                        view_motion.record(before,*world->objects.at(*world->player).pose,double(world->simulation_ticks()));
                    }else if(event==dok::GameEvent::weapon_tick||event==dok::GameEvent::weapon_switch)weapons->event(owner,event);
                    else if(event==dok::GameEvent::projectile_tick)projectiles->event(owner);
                    else if(event==dok::GameEvent::monster_tick)monsters->event(owner);
                    else dok::door_event(*world,owner,event);
                });
                const auto phase=flow->observe();
                if(phase!=dok::SessionPhase::playing){
                    if(phase==dok::SessionPhase::completed){story=dok::paginate_story(sets[current].archive.get(sets[current].archive.identify()==dok::DatasetId::old_gold?"INTRO4E":"EINTRO"+level_name.substr(5)));story_page=0;ending=true;}
                    input.capture_mouse(false);use_pending=false;
                    const char* message=phase==dok::SessionPhase::retry?"Life lost - Enter/A: retry":
                        phase==dok::SessionPhase::game_over?"Game over - Enter/A: level selection":"Level completed - Enter/A: level selection";
                    SDL_SetWindowTitle(window.get(),message);log<<message<<'\n';
                }
                draw_scene();
            }
            if(world&&(paused||pause_changed||flow_changed||!story.empty()||flow->phase()!=dok::SessionPhase::playing))view_motion.reset();
            if(gl){gl->set_linear_filter(linear_filter);gl->begin();std::fill(ui_pixels.begin(),ui_pixels.end(),0);
                if(world){
                    const auto pose=*world->objects.at(*world->player).pose;
                    const auto bounds=dok::view_cell_bounds(pose,world->definition.cell_side,world->definition.width,world->definition.length,gl->view_width());
                    auto dynamic=monsters->images(bounds);const auto shots=projectiles->images();dynamic.insert(dynamic.end(),shots.begin(),shots.end());
                    gl->world(scene->palette());scene->render(*world,pose,dynamic,[&](const auto& vertices,const auto& image,dok::Bytes light,dok::RasterAxis axis){gl->polygon(vertices,image,light,axis);},gl->view_width(),view_motion.offset(world->presentation_ticks()));
                    dok::IndexedSurface overlay(gl->view_width(),160);weapons->overlay(overlay);
                    std::vector<std::uint32_t> pixels(overlay.pixels.size());
                    for(std::size_t i=0;i<pixels.size();++i)if(overlay.pixels[i])pixels[i]=scene->palette()[overlay.pixels[i]];
                    gl->image(pixels,overlay.width(),overlay.height(),{0,0,320,240});
                    int screen_width,screen_height;SDL_GL_GetDrawableSize(window.get(),&screen_width,&screen_height);
                    const int hud_scale=std::max(1,screen_height/720);
                    auto hud_frame=hud->desktop(std::max(320,screen_width/hud_scale),*world->objects.at(*world->player).player_state,
                        scene->palette(),world->visible_message(),world->presentation_ticks()*119.0/dok::GameClock::pit_hz);
                    gl->image(hud_frame.pixels,hud_frame.width,hud_frame.height,
                        {0,screen_height-hud_frame.height*hud_scale,hud_frame.width*hud_scale,hud_frame.height*hud_scale},true);
                }
            }else{SDL_SetRenderDrawColor(renderer.get(),12,14,20,255);SDL_RenderClear(renderer.get());}
            if(texture||(gl&&!world&&!title_pixels.empty())) {
                // All current title images are DOS artwork; correct 320x200 to 4:3.
                double aspect=double(image_width)/(image_height*1.2);
                SDL_Rect rect{0,0,320,static_cast<int>(320/aspect)};
                if(rect.h>240){rect.h=240;rect.w=static_cast<int>(240*aspect);}
                rect.x=(320-rect.w)/2;rect.y=level_name.empty()?(240-rect.h)/2:0;
                if(gl)gl->image(title_pixels,image_width,image_height,rect);else{SDL_SetTextureScaleMode(texture.get(),linear_filter?SDL_ScaleModeLinear:SDL_ScaleModeNearest);SDL_RenderCopy(renderer.get(),texture.get(),nullptr,&rect);}
            }
            if(!story.empty()){
                panel({4,4,312,232});
                for(std::size_t row=0;row<story[story_page].size();++row)line(story[story_page][row],12+static_cast<int>(row)*10);
                line(std::to_string(story_page+1)+" / "+std::to_string(story.size())+"   ENTER / A: CONTINUE",218);
            }else if(world){
                const auto phase=flow->phase();
                if(phase!=dok::SessionPhase::playing||paused){
                    panel({0,88,320,56});
                    line(phase==dok::SessionPhase::retry?"LIFE LOST":phase==dok::SessionPhase::game_over?"GAME OVER":
                        phase==dok::SessionPhase::completed?"LEVEL COMPLETED":"PAUSED",100);
                    line(phase==dok::SessionPhase::retry?"ENTER / A: RETRY":phase==dok::SessionPhase::playing?"ESC / START: CONTINUE":"ENTER / A: LEVEL SELECTION",120);
                }
            }else if(!sets.empty()){
                panel({0,192,320,48});
                line(sets[current].archive.identify()==dok::DatasetId::old_gold?"OLD GOLD - LEVEL4":"UP / DOWN: LEVEL "+std::to_string(menu_level),198);line("ENTER / A: START",212);line("LEFT / RIGHT: DATASET",226);
            }
            ++frames;
            ++fps_frames;
            const auto fps_now=SDL_GetPerformanceCounter();
            const auto fps_elapsed=double(fps_now-fps_time)/SDL_GetPerformanceFrequency();
            if(fps_elapsed>=0.5){fps_value=static_cast<unsigned>(fps_frames/fps_elapsed+0.5);fps_frames=0;fps_time=fps_now;}
            if(show_fps){panel({244,2,76,12});line("FPS "+(fps_value?std::to_string(fps_value):std::string("--")),4,246);}
            if(gl)gl->image(ui_pixels,320,240,{0,0,320,240});
            std::vector<std::string> captures;
            if(frame_limit && frames==frame_limit && !screenshot.empty())captures.push_back(screenshot);
            if(capture_requested){
                try{
                    const auto directory=std::filesystem::current_path()/"screenshots";
                    std::filesystem::create_directories(directory);
                    const auto stamp=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                    captures.push_back((directory/("screenshot-"+std::to_string(stamp)+"-"+std::to_string(frames)+".png")).u8string());
                }catch(const std::exception& error){log<<"Screenshot failed: "<<error.what()<<'\n';}
            }
            for(const auto& capture:captures)try{
                if(gl)gl->screenshot(capture);
                else{
                int w,h;SDL_GetRendererOutputSize(renderer.get(),&w,&h);
                SdlPtr<SDL_Surface,SDL_FreeSurface> surface(SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_ARGB8888),SDL_FreeSurface);
                const SDL_Rect full{0,0,w,h};
                if(!surface || SDL_RenderReadPixels(renderer.get(),&full,SDL_PIXELFORMAT_ARGB8888,surface->pixels,surface->pitch)!=0)throw std::runtime_error(SDL_GetError());
                if(std::filesystem::u8path(capture).extension()==".png"){
                    std::vector<std::uint32_t> pixels(std::size_t(w)*h);
                    for(int y=0;y<h;++y){const auto* row=reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(surface->pixels)+y*surface->pitch);
                        std::copy(row,row+w,pixels.begin()+std::size_t(y)*w);}
                    dok::save_png(std::filesystem::u8path(capture),w,h,pixels);
                }else if(SDL_SaveBMP(surface.get(),capture.c_str())!=0)throw std::runtime_error(SDL_GetError());
                }
                log<<"Screenshot saved: "<<capture<<'\n';
            }catch(const std::exception& error){if(!capture_requested)throw;log<<"Screenshot failed: "<<error.what()<<'\n';}
            if(gl)gl->present();else SDL_RenderPresent(renderer.get());
            if(frame_limit && frames>=frame_limit)running=false;
            SDL_Delay(1);
        }
        log<<"Clean shutdown; frames="<<frames<<" datasets="<<sets.size()<<'\n';
        if(world){const auto& pose=*world->objects.at(*world->player).pose;log<<"Player x="<<pose.x<<" z="<<pose.z<<" heading="<<pose.heading<<" ticks="<<world->simulation_ticks()<<'\n';}
        std::cout<<"SDL smoke completed: "<<frames<<" frames, "<<sets.size()<<" datasets\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
