// Deterministic presentation fixtures, not a gameplay replay or device emulator.
#include "dok/gl_renderer.hpp"
#include "dok/monsters.hpp"
#include "dok/weapons.hpp"
#include "dok/hud.hpp"
#include "dok/png.hpp"
#include <SDL_opengl.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>

namespace {
using Clock=std::chrono::steady_clock;
double ms(Clock::time_point a,Clock::time_point b){return std::chrono::duration<double,std::milli>(b-a).count();}
std::string quote(const std::string& s){
    std::ostringstream o;o<<'"';
    for(unsigned char c:s){if(c=='"'||c=='\\')o<<'\\'<<c;else if(c<32)o<<"\\u00"<<"0123456789abcdef"[c>>4]<<"0123456789abcdef"[c&15];else o<<c;}
    return o.str()+'"';
}
void require(bool b,const char* error){if(!b)throw std::runtime_error(error);}
void bytes(const std::filesystem::path& p,const std::vector<std::uint8_t>& data){
    std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<const char*>(data.data()),data.size());require(bool(f),"Cannot write capture");
}
std::vector<std::uint8_t> rgba(const std::vector<std::uint32_t>& words){
    std::vector<std::uint8_t> out;out.reserve(words.size()*4);
    for(auto c:words){out.push_back(c>>16);out.push_back(c>>8);out.push_back(c);out.push_back(c>>24);}return out;
}
struct Case {std::string id;dok::Pose pose;int door=-1,phase=0;std::uint64_t ticks=0;std::array<double,4> offset{};
    // Inclusive runtime-map cell rectangle; explicit synchronized volley fixture.
    bool volley=false;dok::CellBounds arena{};};
std::string presentation_state(const dok::World& world){
    // Canonical logical values, never host struct bytes/pointers/padding.
    std::uint64_t hash=14695981039346656037ULL;
    auto value=[&](std::uint64_t v){for(int i=0;i<8;++i){hash=(hash^(v&255))*1099511628211ULL;v>>=8;}};
    auto pose=[&](dok::Pose p){value(p.x);value(p.y);value(p.z);value(p.heading);};
    value(world.simulation_ticks());
    for(const auto& map:world.maps){for(auto c:map.first)value(static_cast<unsigned char>(c));value(map.second.size());for(auto c:map.second)value(c);}
    for(const auto& o:world.objects){
        value(o.owner);value(o.flags);value(bool(o.pose));if(o.pose)pose(*o.pose);
        if(o.door){value(o.door->phase);value(o.door->extent);}
        if(o.monster){value(o.monster->health);value(o.monster->state);value(o.monster->display_clip);value(o.monster->display_frame);value(o.monster->corpse);}
        if(o.weapon){value(o.weapon->frame);value(o.weapon->shift);value(o.weapon->state);value(o.weapon->acquired);}
        if(o.player_state){for(auto a:o.player_state->attributes)value(a);for(auto a:o.player_state->weapons)value(a);value(o.player_state->current_weapon);value(o.player_state->score);}
    }
    for(const auto& p:world.projectiles){value(bool(p));if(p){pose(p->pose);value(p->definition_index);value(p->flags);value(p->frame);value(p->impact);value(p->actor_impact);value(p->effect_frame);}}
    for(unsigned char c:world.visible_message())value(c);
    std::ostringstream out;out<<std::hex<<hash;return out.str();
}
void stress_cases(std::vector<Case>& result,const dok::World& world){
    if(world.definition.dataset==dok::DatasetId::old_gold)return;
    const auto& d=world.definition;
    auto room=[&](const std::string& name,int x,int z,int angle,dok::CellBounds arena,bool volley){
        require(x>=0&&x<d.width&&z>=0&&z<d.length,"Stress room outside map");
        require(d.object(world.maps.at("tmap")[z*d.width+x])->class_id!=2,"Stress camera inside solid wall");
        for(int step:volley?std::vector<int>{0,1,2,4,8,16,32}:std::vector<int>{0}){
            Case c{name+"-tick-"+std::to_string(step*416),{x*d.cell_side+d.cell_side/2,-300,z*d.cell_side+d.cell_side/2,static_cast<std::int16_t>(angle)}};
            c.volley=volley;c.arena=arena;c.ticks=step*416;result.push_back(c);
        }
    };
    if(d.name=="LEVEL1"){
        require(d.object(world.maps.at("fcmap")[52*d.width+57])->number(0x54)==400,"Low ceiling fixture changed");
        for(int a:{0,45,89,90,91,135,180,225,270,315})room("low-ceiling-"+std::to_string(a),57,52,a,{},false);
        room("low-ceiling-west",20,33,90,{},false);
    }else if(d.name=="LEVEL2"){
        // Verified against SDL2 World maps; GBA reports supplied location hints only.
        room("final-volley",31,50,180,{13,47,49,48},true);
        room("final-volley-diagonal",31,50,135,{13,47,49,48},true);
        room("ammo-entrance",50,41,270,{},false);
        room("ammo-diagonal",51,41,315,{},false);
        room("ammo-volley",51,41,270,{50,40,59,43},true);
    }else if(d.name=="LEVEL3"){
        room("shotgun-witches-volley",20,30,270,{13,24,25,36},true);
        room("shotgun-witches-diagonal",20,30,315,{13,24,25,36},true);
    }
}
std::vector<Case> cases(const dok::World& world,const std::string& suite){
    const bool full=suite=="full";
    const auto start=*world.objects.at(*world.player).pose;
    std::vector<Case> result{{"initial",start}};
    for(int angle: full?std::vector<int>{0,45,90,135,180,225,270,315}:std::vector<int>{45}){
        auto p=start;p.heading=angle;result.push_back({"heading-"+std::to_string(angle),p});
    }
    Case fractional{"fractional",start};fractional.offset={0.25,0,0.75,0.125};result.push_back(fractional);
    if(suite=="stress"){result.clear();stress_cases(result,world);return result;}
    if(!full)return result;
    stress_cases(result,world);
    std::set<std::pair<std::string,unsigned>> doors;unsigned water=0;const auto& d=world.definition;
    for(std::size_t cell=0;cell<world.maps.at("tmap").size();++cell){
        const int x=int(cell%d.width)*d.cell_side+d.cell_side/2,z=int(cell/d.width)*d.cell_side+d.cell_side/2;
        const int index=d.glyph_index[world.maps.at("tmap")[cell]];
        const auto& object=d.objects.at(index);
        if(world.objects.at(index).door&&doors.emplace(object.sprite,object.sprite_flags).second){
            for(int phase=0;phase<30;++phase)result.push_back({"door-"+std::to_string(index)+"-"+std::to_string(phase),
                {x,start.y,z-d.cell_side,0},index,phase});
        }
        const auto& floor=*d.object(world.maps.at("fcmap")[cell]);const unsigned flags=floor.sprite_flags&0x3c00;
        if(d.dataset!=dok::DatasetId::old_gold&&floor.class_id==12&&flags&&!(water&flags)){
            water|=flags;
            for(int phase=0;phase<18;++phase)result.push_back({"water-"+std::to_string(flags)+"-"+std::to_string(phase),
                {x,-300,z,0},-1,0,std::uint64_t(phase)*334});
        }
    }
    return result;
}
void samples(std::ostream& out,const std::vector<double>& values){
    auto sorted=values;std::sort(sorted.begin(),sorted.end());
    auto p=[&](double q){return sorted.at(std::size_t(std::ceil(q*sorted.size()))-1);};
    double sum=0;for(auto v:values)sum+=v;
    out<<"{\"mean\":"<<sum/values.size()<<",\"p50\":"<<p(.5)<<",\"p95\":"<<p(.95)<<",\"p99\":"<<p(.99)<<",\"max\":"<<sorted.back()<<",\"samples\":[";
    for(std::size_t i=0;i<values.size();++i){if(i)out<<',';out<<values[i];}out<<"]}";
}
struct Sdl {Sdl(){SDL_SetMainReady();require(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER)==0,SDL_GetError());}~Sdl(){SDL_Quit();}};
struct Window {SDL_Window* p=nullptr;~Window(){if(p)SDL_DestroyWindow(p);}};
}
int main(int argc,char** argv){try{
    if(argc!=11&&argc!=12)throw std::invalid_argument("Usage: dok_render_bench RES OUT LEVEL|all software|gl W H nearest|linear quick|full|stress WARMUP SAMPLES [profile]");
    const bool profile=argc==12;require(!profile||std::string(argv[11])=="profile","Unknown diagnostic mode");
    const auto archive=dok::Archive::open(std::filesystem::u8path(argv[1]));
    const auto root=std::filesystem::u8path(argv[2]);const std::string level_arg=argv[3],backend=argv[4],filter=argv[7],suite=argv[8];
    const int width=std::stoi(argv[5]),height=std::stoi(argv[6]),warmup=std::stoi(argv[9]),iterations=std::stoi(argv[10]);
    const std::array<std::string,7> filters{"nearest","linear","mipmaps","trilinear","aniso2","aniso4","aniso8"};
    const auto selected=std::find(filters.begin(),filters.end(),filter);
    require((backend=="gl"||backend=="software")&&selected!=filters.end()&&(backend=="gl"||selected-filters.begin()<2)&&(suite=="quick"||suite=="full"||suite=="stress"),"Invalid configuration");
    require(!(suite=="stress"&&archive.identify()==dok::DatasetId::old_gold),"Stress rooms are Kremlin-specific; use quick/full for Old Gold");
    require(width>=1&&width<=4096&&height>=1&&height<=4096&&warmup>=1&&iterations>=1&&iterations<=100000,"Invalid dimensions/counts");
    require(!std::filesystem::exists(root),"Output exists; use a new directory");std::filesystem::create_directories(root);
    Sdl sdl;Window window;std::unique_ptr<dok::GlRenderer> gl;
    using Finish=decltype(&glFinish);using Read=decltype(&glReadPixels);using Get=decltype(&glGetString);using Error=decltype(&glGetError);
    Finish finish=nullptr;Read read=nullptr;Error error=nullptr;std::string vendor,renderer,version;int swap_interval=0;
    if(backend=="gl"){
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,2);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,0);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,24);SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);
        window.p=SDL_CreateWindow("DoK render benchmark",0,0,width,height,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
        require(window.p,SDL_GetError());gl=std::make_unique<dok::GlRenderer>(window.p);
        SDL_GL_SetSwapInterval(0);swap_interval=SDL_GL_GetSwapInterval();gl->set_texture_filter(int(selected-filters.begin()));
        finish=reinterpret_cast<Finish>(SDL_GL_GetProcAddress("glFinish"));read=reinterpret_cast<Read>(SDL_GL_GetProcAddress("glReadPixels"));
        auto get=reinterpret_cast<Get>(SDL_GL_GetProcAddress("glGetString"));error=reinterpret_cast<Error>(SDL_GL_GetProcAddress("glGetError"));
        require(finish&&read&&get&&error,"Missing GL benchmark functions");
        vendor=reinterpret_cast<const char*>(get(GL_VENDOR));renderer=reinterpret_cast<const char*>(get(GL_RENDERER));version=reinterpret_cast<const char*>(get(GL_VERSION));
        int w,h;SDL_GL_GetDrawableSize(window.p,&w,&h);require(w==width&&h==height,"Drawable differs from requested size");
    }
    std::ofstream report(root/"native.json");require(bool(report),"Cannot write report");report.precision(9);
    SDL_version sdl_version{};SDL_GetVersion(&sdl_version);
    report<<"{\"schema\":1,\"fixture_version\":4,\"dataset\":"<<quote(dok::dataset_name(archive.identify()))
        <<",\"backend\":"<<quote(backend)<<",\"width\":"<<width<<",\"height\":"<<height<<",\"filter\":"<<quote(filter)
        <<",\"suite\":"<<quote(suite)<<",\"warmup\":"<<warmup<<",\"iterations\":"<<iterations<<",\"profile_submission\":"<<(profile?"true":"false")
        <<",\"build_type\":"<<quote(DOK_BENCH_BUILD_TYPE)<<",\"compiler\":"<<quote(
#ifdef __clang__
        __clang_version__
#elif defined(__GNUC__)
        __VERSION__
#else
        "unknown"
#endif
        )<<",\"platform\":"<<quote(SDL_GetPlatform())<<",\"pointer_bits\":"<<sizeof(void*)*8
        <<",\"sdl_version\":"<<quote(std::to_string(sdl_version.major)+"."+std::to_string(sdl_version.minor)+"."+std::to_string(sdl_version.patch))
        <<",\"cpu_count\":"<<SDL_GetCPUCount()<<",\"ram_mb\":"<<SDL_GetSystemRAM()
        <<",\"gl_vendor\":"<<quote(vendor)<<",\"gl_renderer\":"<<quote(renderer)<<",\"gl_version\":"<<quote(version)<<",\"swap_interval\":"<<swap_interval<<",\"cases\":[";
    std::vector<std::string> levels=archive.identify()==dok::DatasetId::old_gold?std::vector<std::string>{"LEVEL4"}:std::vector<std::string>{"LEVEL1","LEVEL2","LEVEL3"};
    if(level_arg!="all")levels={level_arg};bool first=true;
    for(const auto& level:levels){
        const auto definition=dok::load_level(archive,level);dok::World initial(definition);
        // Preserve the scene between cases to exercise phase/material cache invalidation.
        dok::SoftwareScene scene(archive,definition);if(!gl)scene.software_resolution(width,height);
        for(const auto& c:cases(initial,suite)){
            dok::World world(definition);
            // Explicit fixture clock advance: gameplay callbacks intentionally do not run.
            const auto ns=(c.ticks*dok::GameClock::denominator+dok::GameClock::pit_hz-1)/dok::GameClock::pit_hz;
            if(!c.volley)world.advance_events(ns,[](auto,auto){});
            if(c.door>=0){auto& door=*world.objects.at(c.door).door;door.extent=30;door.phase=c.phase;}
            dok::Weapons weapons(world,archive);dok::Projectiles projectiles(world,archive);dok::Monsters monsters(world,archive,projectiles);dok::Hud hud(archive);
            weapons.grant_all();for(auto& o:world.objects)if(o.weapon&&o.weapon->acquired){o.weapon->state=5;o.weapon->shift=0;}
            unsigned shooters=0;
            if(c.volley){
                auto& hero=world.objects.at(*world.player);
                auto& mmap=world.maps.at("mmap");const auto hero_glyph=definition.objects[hero.definition_index].glyph;
                for(auto& glyph:mmap)if(glyph==hero_glyph)glyph=' ';
                hero.pose=c.pose;const int hero_cell=(c.pose.z/definition.cell_side)*definition.width+c.pose.x/definition.cell_side;
                require(mmap.at(hero_cell)==' ',"Volley camera overlaps actor");mmap[hero_cell]=hero_glyph;
                hero.player_state->attributes[0]=hero.player_state->attributes[2]=32767;
                auto inside=[&](dok::Pose p){const int x=p.x/definition.cell_side,z=p.z/definition.cell_side;return x>=c.arena.min_x&&x<=c.arena.max_x&&z>=c.arena.min_z&&z<=c.arena.max_z;};
                // Open arena doors as a declared fixture precondition, not a gameplay shortcut.
                for(int z=c.arena.min_z;z<=c.arena.max_z;++z)for(int x=c.arena.min_x;x<=c.arena.max_x;++x){
                    auto& object=world.objects.at(definition.glyph_index[world.maps.at("tmap")[z*definition.width+x]]);
                    if(object.door){object.door->extent=30;object.door->phase=29;object.flags|=8;}
                }
                for(auto& o:world.objects)if(o.monster&&o.pose&&inside(*o.pose)){
                    const auto& d=definition.objects[o.definition_index];
                    monsters.wake(o.owner);
                    if(d.number(0xa6)<=0)continue;
                    o.pose->heading=static_cast<std::int16_t>((360-dok::original_direction(hero.pose->z-o.pose->z,hero.pose->x-o.pose->x))%360);
                    require(projectiles.spawn(o.owner,static_cast<std::uint8_t>(d.number(0xe7)))>0,"Synchronized volley allocation failed");++shooters;
                }
                require(shooters>0,"Volley fixture has no shooters");
                // Real SDL2 callbacks after the forced simultaneous first volley; deterministic 416-tick steps.
                std::uint64_t elapsed=0;
                for(std::uint64_t tick=416;tick<=c.ticks;tick+=416){
                    const auto target=(tick*dok::GameClock::denominator+dok::GameClock::pit_hz-1)/dok::GameClock::pit_hz;
                    world.advance_events(target-elapsed,[&](auto owner,auto callback){
                        const auto event=static_cast<dok::GameEvent>(callback);
                        if(event==dok::GameEvent::monster_tick)monsters.event(owner);
                        else if(event==dok::GameEvent::projectile_tick)projectiles.event(owner);
                        else if(event==dok::GameEvent::weapon_tick||event==dok::GameEvent::weapon_switch)weapons.event(owner,event);
                    });elapsed=target;
                }
            }
            require(world.simulation_ticks()==c.ticks,"Fixture clock mismatch");
            unsigned impacts=0;for(const auto& p:world.projectiles)if(p&&p->impact)++impacts;
            const auto state_hash=presentation_state(world);
            std::vector<std::uint8_t> indices;std::vector<std::uint32_t> colors;
            std::array<std::vector<double>,5> timings;
            std::size_t draws=0,vertices=0,picture_bytes=0;
            double submission_total=0,flush_total=0;
            auto render=[&](bool measure){
                SDL_PumpEvents();if(gl)finish();const auto t0=Clock::now();if(gl)gl->begin();
                const auto bounds=gl?dok::view_cell_bounds(c.pose,definition.cell_side,definition.width,definition.length,gl->view_width()):dok::original_cell_bounds(c.pose,definition.cell_side,definition.width,definition.length);
                auto dynamic=monsters.images(bounds);const auto shots=projectiles.images();dynamic.insert(dynamic.end(),shots.begin(),shots.end());
                const auto t1=Clock::now();draws=vertices=picture_bytes=0;
                std::optional<dok::IndexedSurface> frame;
                double submission=0,flush=0;
                if(gl){gl->world(scene.palette(),true,true);scene.render(world,c.pose,dynamic,[&](const auto& v,const auto& image,auto light,auto axis){
                    ++draws;vertices+=v.size();picture_bytes+=image.pixels.size();
                    if(profile){const auto begin=Clock::now();gl->polygon(v,image,light,axis);submission+=ms(begin,Clock::now());}
                    else gl->polygon(v,image,light,axis);
                },gl->view_width(),c.offset);
                    if(profile){const auto begin=Clock::now();gl->flush_world();flush=ms(begin,Clock::now());}else gl->flush_world();
                }else frame=scene.render(world,c.pose,dynamic);
                const auto t2=Clock::now();
                if(gl){
                    dok::IndexedSurface overlay(gl->view_width(),160);weapons.overlay(overlay);std::vector<std::uint32_t> pixels(overlay.pixels.size());
                    for(std::size_t i=0;i<pixels.size();++i)if(overlay.pixels[i])pixels[i]=scene.palette()[overlay.pixels[i]];
                    gl->image(pixels,overlay.width(),overlay.height(),{0,0,320,240});
                    const int scale=std::max(1,height/720);const auto& h=hud.desktop(std::max(320,width/scale),*world.objects.at(*world.player).player_state,scene.palette(),world.visible_message(),1.0);
                    gl->image(h.pixels,h.width,h.height,{0,height-h.height*scale,h.width*scale,h.height*scale},true);
                }else{
                    weapons.overlay(*frame,height);*frame=hud.classic_mode(*frame,height,*world.objects.at(*world.player).player_state,world.visible_message());
                    colors.resize(frame->pixels.size());std::transform(frame->pixels.begin(),frame->pixels.end(),colors.begin(),[&](auto p){return scene.palette()[p];});
                }
                const auto t3=Clock::now();if(gl)finish();const auto t4=Clock::now();
                if(measure){const std::array<double,5> times{ms(t0,t1),ms(t1,t2),ms(t2,t3),ms(t3,t4),ms(t0,t4)};for(int i=0;i<5;++i)timings[i].push_back(times[i]);}
                if(measure&&profile){submission_total+=submission;flush_total+=flush;}
                // Capture storage is outside the timed range.
                if(!gl)indices=std::move(frame->pixels);
            };
            auto capture=[&]{
                if(!gl)return rgba(colors);
                std::vector<std::uint8_t> bottom(std::size_t(width)*height*4),top(bottom.size());
                read(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,bottom.data());require(error()==GL_NO_ERROR,"GL error during benchmark/capture");
                for(int y=0;y<height;++y)std::copy_n(bottom.data()+std::size_t(height-1-y)*width*4,width*4,top.data()+std::size_t(y)*width*4);return top;
            };
            for(int i=0;i<warmup;++i)render(false);const auto reference=capture(),reference_indices=indices;
            for(int i=0;i<iterations;++i)render(true);
            require(capture()==reference&&indices==reference_indices,"Frozen fixture is not deterministic across redraws");
            require(presentation_state(world)==state_hash,"Renderer mutated logical presentation state");
            const auto id=level+"-"+c.id;bytes(root/(id+".rgba"),reference);if(!gl)bytes(root/(id+".idx"),reference_indices);
            std::vector<std::uint32_t> png(std::size_t(width)*height);
            for(std::size_t i=0;i<png.size();++i)png[i]=(std::uint32_t(reference[i*4+3])<<24)|(reference[i*4]<<16)|(reference[i*4+1]<<8)|reference[i*4+2];
            dok::save_png(root/(id+".png"),width,height,png);
            if(!first)report<<',';first=false;
            report<<"{\"id\":"<<quote(id)<<",\"pose\":["<<c.pose.x<<','<<c.pose.y<<','<<c.pose.z<<','<<c.pose.heading<<"],\"offset\":["<<c.offset[0]<<','<<c.offset[1]<<','<<c.offset[2]<<','<<c.offset[3]
                <<"],\"ticks\":"<<c.ticks<<",\"door\":"<<c.door<<",\"phase\":"<<c.phase<<",\"volley\":"<<(c.volley?"true":"false")
                <<",\"arena\":["<<c.arena.min_x<<','<<c.arena.min_z<<','<<c.arena.max_x<<','<<c.arena.max_z<<"],\"shooters\":"<<shooters
                <<",\"active_projectiles\":"<<projectiles.active()<<",\"impacts\":"<<impacts<<",\"presentation_state_fnv64\":"<<quote(state_hash)<<",\"polygons\":"<<scene.polygon_count()
                <<",\"world_polygon_submissions\":"<<draws<<",\"submitted_vertices\":"<<vertices<<",\"submitted_image_bytes\":"<<picture_bytes<<",\"timings_ms\":{";
            const char* names[]={"prepare","scene","ui_convert","gpu_wait","total"};for(int i=0;i<5;++i){if(i)report<<',';report<<quote(names[i])<<':';samples(report,timings[i]);}report<<"}}";
            std::cout<<dok::dataset_name(archive.identify())<<' '<<backend<<' '<<id;
            if(profile){double scene_total=0;for(auto t:timings[1])scene_total+=t;
                std::cout<<" geometry_ms="<<(scene_total-submission_total-flush_total)/iterations<<" polygon_ms="<<submission_total/iterations<<" flush_ms="<<flush_total/iterations;}
            std::cout<<'\n';
        }
    }
    report<<"]}\n";require(bool(report),"Report write failed");return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
