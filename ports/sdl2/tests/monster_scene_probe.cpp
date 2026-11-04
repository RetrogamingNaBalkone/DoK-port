#include "dok/monsters.hpp"
#include "dok/display.hpp"
#include "dok/gpu_geometry.hpp"
#include "dok/png.hpp"
#include <SDL_opengl.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>

static void check(bool okay,const char* message){if(!okay)throw std::runtime_error(message);}
int main(int argc,char** argv){
    SDL_SetMainReady();if(SDL_Init(SDL_INIT_VIDEO))return 1;
    try{
        if(argc!=3)throw std::runtime_error("Usage: dok_monster_scene_probe OLD_GOLD.RES output-directory");
        const auto archive=dok::Archive::open(std::filesystem::u8path(argv[1]));
        dok::World world(dok::load_level(archive,"LEVEL4"));dok::Projectiles shots(world,archive);dok::Monsters monsters(world,archive,shots);
        auto found=std::find_if(world.objects.begin(),world.objects.end(),[](const auto& o){return o.monster&&o.pose;});
        check(found!=world.objects.end(),"No placed OG monster");const auto owner=found->owner;const auto initial=*found->pose;
        monsters.wake(owner);monsters.damage(owner,found->monster->health);
        const auto deadline=world.events.events(owner).front().deadline;
        world.advance_events((std::uint64_t(deadline)*119000000000ULL+1193181)/1193182,[&](auto id,auto event){
            if(event==static_cast<dok::CallbackId>(dok::GameEvent::monster_tick))monsters.event(id);
        });
        check(found->pose->y==-75,"Expected native height150 ->75 on first death frame");
        auto dynamic=monsters.images({0,0,63,63});auto instance=std::find_if(dynamic.begin(),dynamic.end(),[&](const auto& p){return p.pose.x==initial.x&&p.pose.z==initial.z;});
        check(instance!=dynamic.end()&&instance->pose.y==-75,"Dynamic getter discarded death height");
        const auto& image=*instance->image;const auto out=std::filesystem::u8path(argv[2]);std::filesystem::create_directories(out);
        std::array<std::uint8_t,4096> table{};for(unsigned i=0;i<table.size();++i)table[i]=i%256;
        const dok::Bytes light{table.data(),table.size()};std::array<std::uint32_t,256> palette{};
        for(unsigned i=1;i<256;++i)palette[i]=0xffffffffu;palette[0]=0xff000000u;
        // Isolate the original death image and its dynamic origin. Test palette
        // and clear background make the vertical anchor measurable, not a DOS frame.
        const dok::Pose camera{initial.x,-200,initial.z-800,0};
        dok::SoftwareCamera software({320,170,8,4096,200});software.set_pose(camera);
        dok::GpuCamera gpu(camera,320,0,{},170);
        dok::VideoSettings settings;settings.width=640;settings.height=340;dok::Display display(settings);
        const auto read=reinterpret_cast<decltype(&glReadPixels)>(SDL_GL_GetProcAddress("glReadPixels"));
        int previous_sw=-1,previous_gl=-1;
        for(bool corrected:{false,true}){
            auto pose=instance->pose;if(!corrected)pose.y=initial.y;
            const auto sw=dok::billboard(software,pose,image.width*instance->scale,image.height*instance->scale,image.width,image.height,8);
            const auto hw=gpu.sprite(pose,image.width*instance->scale,image.height*instance->scale,image.width,image.height,8);
            check(sw&&hw,"Death image outside camera");dok::IndexedSurface surface(320,170);
            dok::rasterize_old_gold(surface,sw->vertices,image,light,software.clip(),0,dok::RasterAxis::vertical);
            int sw_bottom=0;for(int y=0;y<170;++y)for(int x=0;x<320;++x)if(surface.pixels[y*320+x])sw_bottom=std::max(sw_bottom,y);
            std::vector<std::uint32_t> pixels(surface.pixels.size());std::transform(surface.pixels.begin(),surface.pixels.end(),pixels.begin(),[&](auto i){return palette[i];});
            dok::save_png(out/(corrected?"software-corrected.png":"software-old-anchor.png"),320,170,pixels);
            display.begin();auto* gl=display.gl();gl->scene_view({0,0,640,340},320,170);gl->world(palette);gl->polygon(hw->vertices,image,light,dok::RasterAxis::vertical);gl->finish_scene(false);
            std::vector<std::uint8_t> rgba(640*340*4);read(0,0,640,340,GL_RGBA,GL_UNSIGNED_BYTE,rgba.data());
            int gl_bottom=0;for(int y=0;y<340;++y)for(int x=0;x<640;++x)if(rgba[(y*640+x)*4])gl_bottom=std::max(gl_bottom,339-y);
            display.screenshot((out/(corrected?"gl-corrected.png":"gl-old-anchor.png")).string());
            if(corrected){check(sw_bottom>previous_sw,"Software did not sink the death image");check(gl_bottom>previous_gl,"Real GL did not sink the death image");}
            previous_sw=sw_bottom;previous_gl=gl_bottom;
            std::cout<<(corrected?"corrected":"old-anchor")<<" dynamic_y="<<pose.y<<" software_bottom="<<sw_bottom<<" gl_bottom="<<gl_bottom<<'\n';
        }
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';SDL_Quit();return 1;}
    SDL_Quit();return 0;
}
