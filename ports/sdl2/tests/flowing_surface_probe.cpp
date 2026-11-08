#include "dok/scene.hpp"
#include "dok/gl_renderer.hpp"
#include "dok/gl_api.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
static void check(bool b,const char* message){if(!b)throw std::runtime_error(message);}
int main(int argc,char** argv){try{
    // Non-divisible dimensions distinguish divide-then-multiply from smooth UV scrolling.
    dok::Image source{38,56,8,std::vector<std::uint8_t>(38*56)};
    for(std::size_t i=0;i<source.pixels.size();++i)source.pixels[i]=i%251;
    for(unsigned flags:{0x0400u,0x0800u,0x1000u,0x2000u}){
        source.flags=8|flags;
        const auto moved=dok::flowing_surface_image(source,334);
        const int sx=flags==0x0400?36:flags==0x0800?2:0;
        const int sy=flags==0x1000?53:flags==0x2000?3:0;
        check(moved.pixels[0]==source.pixels[sy*38+sx],"Wrong crop direction/rounding");
        check(dok::flowing_surface_image(source,333).pixels==source.pixels,"Early phase");
        check(dok::flowing_surface_image(source,334*18).pixels==source.pixels,"Cycle did not wrap");
        check(dok::flowing_surface_image(source,334).pixels==moved.pixels,"Crop accumulates on redraw");
    }
    if(argc==1){std::cout<<"Flowing surface crop passed\n";return 0;}
    SDL_SetMainReady();check(SDL_Init(SDL_INIT_VIDEO)==0,"SDL init");
    dok::configure_gl_context();
    auto* window=SDL_CreateWindow("Water regression",0,0,640,384,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
    check(window,"GL window");
    {
        dok::GlRenderer gl(window);
        const auto read=reinterpret_cast<decltype(&glReadPixels)>(SDL_GL_GetProcAddress("glReadPixels"));
        for(int arg=1;arg<argc;++arg){
            const auto archive=dok::Archive::open(std::filesystem::u8path(argv[arg]));
            for(const auto* name:{"LEVEL2"}){
                dok::World world(dok::load_level(archive,name));dok::SoftwareScene scene(archive,world.definition);
                unsigned directions=0;
                for(std::size_t cell=0;cell<world.maps.at("fcmap").size();++cell){
                    const auto& d=*world.definition.object(world.maps.at("fcmap")[cell]);
                    const unsigned direction=d.sprite_flags&0x3c00;
                    if(d.class_id!=12||!direction||(directions&direction))continue;
                    directions|=direction;
                    const int side=world.definition.cell_side;
                    dok::Pose pose{int(cell%world.definition.width)*side+side/2,-300,
                                   int(cell/world.definition.width)*side+side/2,0};
                    const auto software0=scene.render(world,pose).pixels;
                    auto gpu_frame=[&]{
                        gl.begin();gl.world(scene.palette());
                        scene.render(world,pose,{},[&](const auto& vertices,const auto& image,auto light,auto axis){gl.polygon(vertices,image,light,axis);});
                        std::vector<std::uint8_t> pixels(640*384*4);read(0,0,640,384,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());return pixels;
                    };
                    const auto gl0=gpu_frame();
                    world.advance_events(100000000,[](auto,auto){});
                    check(scene.render(world,pose).pixels!=software0,"Real water software frame stayed static");
                    const auto gl1=gpu_frame();check(gl1!=gl0,"Real water OpenGL frame stayed static");
                    check(gpu_frame()==gl1,"Repeated draw advances water while paused");
                    for(unsigned fps:{30u,60u,144u}){
                        dok::World timed(world.definition);
                        for(unsigned frame=0;frame<fps;++frame)
                            timed.advance_events((std::uint64_t(frame+1)*1000000000/fps)-(std::uint64_t(frame)*1000000000/fps),[](auto,auto){});
                        const auto pixels=scene.render(timed,pose).pixels;
                        if(fps==30)world=std::move(timed);
                        check(pixels==scene.render(world,pose).pixels,"Water phase depends on presentation FPS");
                    }
                }
                check(directions==0x3c00,"Missing original water directions");
                // Reuse one scene across phase reversal and divisor changes,
                // comparing against an uncached scene on every transition.
                for(auto& object:world.objects)if(object.door){
                    const auto saved=*object.door;
                    const auto glyph=world.definition.objects[object.definition_index].glyph;
                    const auto& map=world.maps.at("tmap");
                    const auto found=std::find(map.begin(),map.end(),glyph);
                    if(found==map.end())continue;
                    const auto cell=std::size_t(found-map.begin());
                    const int side=world.definition.cell_side;
                    dok::Pose pose{int(cell%world.definition.width)*side+side/2,-300,
                        int(cell/world.definition.width)*side+side/2+side,0};
                    for(auto key:{std::pair<int,int>{1,30},{1,18},{17,18},{0,30},{29,30},{1,30}}){
                        object.door->phase=key.first;object.door->extent=key.second;
                        dok::SoftwareScene fresh(archive,world.definition);
                        const auto expected=fresh.render(world,pose).pixels;
                        check(scene.render(world,pose).pixels==expected,"Door phase/extent cache differs from fresh scene");
                        check(scene.render(world,pose).pixels==expected,"Door winding changed on redraw");
                    }
                    *object.door=saved;
                }
                std::cout<<argv[arg]<<' '<<name<<": four water directions, software/GL passed\n";
            }
        }
    }
    SDL_DestroyWindow(window);SDL_Quit();return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
