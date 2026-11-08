#include "dok/display.hpp"
#include "dok/filters.hpp"
#include "dok/scene.hpp"
#include "dok/gl_api.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
using Clock=std::chrono::steady_clock;
static void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
static double ms(Clock::time_point a,Clock::time_point b){return std::chrono::duration<double,std::milli>(b-a).count();}
static double median(std::vector<double> v){std::sort(v.begin(),v.end());return v[v.size()/2];}
int main(int argc,char** argv){
    SDL_SetMainReady();if(SDL_Init(SDL_INIT_VIDEO))return 2;
    try{
        check(argc==3||(argc==4&&std::string(argv[3])=="--measure-only"),"Usage: dok_xbr_probe ROOT OUT [--measure-only]");const bool measure_only=argc==4;const auto root=std::filesystem::u8path(argv[1]),out=std::filesystem::u8path(argv[2]);std::filesystem::create_directories(out);
        dok::configure_gl_context();
        auto* window=SDL_CreateWindow("xBR GPU probe",0,0,960,480,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);check(window,SDL_GetError());
        {
            dok::GlRenderer gl(window);SDL_GL_SetSwapInterval(0);
            auto finish=reinterpret_cast<decltype(&glFinish)>(SDL_GL_GetProcAddress("glFinish"));check(finish,"Missing glFinish");
            auto compare=[&](const std::vector<std::uint32_t>& source,int w,int h,const char* name){
                const auto cpu=dok::xbr3(source,w,h);const SDL_Rect target{7,9,w*3,h*3};
                gl.begin();gl.image(cpu,w*3,h*3,target,true,0);const auto reference=gl.read_pixels();
                gl.begin();gl.image(source,w,h,target,true,3);check(gl.xbr_gpu_ready(),"xBR GPU shader/FBO unavailable");const auto gpu=gl.read_pixels();
                unsigned maximum=0,over1=0;for(std::size_t i=0;i<gpu.size();++i)for(int shift:{0,8,16}){const auto d=unsigned(std::abs(int((reference[i]>>shift)&255)-int((gpu[i]>>shift)&255)));maximum=std::max(maximum,d);over1+=d>1;}
                std::cout<<name<<" CPU/GPU max channel delta="<<maximum<<"; channels >1="<<over1<<'\n';if(!measure_only)check(over1==0,"GPU xBR changed contours beyond rounding tolerance");
                SDL_SetHint("DOK_DISABLE_GPU_XBR","1");gl.begin();gl.image(source,w,h,target,true,3);check(gl.read_pixels()==reference,"CPU fallback changed pixels");
                SDL_SetHint("DOK_XBR_CPU_TILES","1");gl.begin();gl.image(source,w,h,target,true,3);check(gl.read_pixels()==reference,"Small GL texture limit fallback changed pixels");
                SDL_SetHint("DOK_XBR_CPU_TILES","0");SDL_SetHint("DOK_DISABLE_GPU_XBR","0");
            };
            std::vector<std::uint32_t> pattern(13*11);for(int y=0;y<11;++y)for(int x=0;x<13;++x)pattern[y*13+x]=x>y?0xffffffff:0xff000000;
            compare(pattern,13,11,"diagonal");for(auto& p:pattern)if(p==0xff000000)p=0;compare(pattern,13,11,"alpha diagonal");
            std::uint32_t random=42;for(auto& p:pattern){random=random*1664525u+1013904223u;p=0xff000000|(random&0xffffff);}compare(pattern,13,11,"random colors");
            const auto archive=dok::Archive::open(root/"originals/pkr/RUS/PKR.RES");dok::World world(dok::load_level(archive,"LEVEL1"));dok::SoftwareScene scene(archive,world.definition);
            std::ofstream report(out/"timings.json");report<<"{\"cases\":[";bool first=true;
            for(const auto size:{std::array<int,2>{320,160},std::array<int,2>{640,384},std::array<int,2>{1024,614}}){
                const int w=size[0],h=size[1];scene.software_presentation(w,h,160);const auto& indexed=scene.render(world,*world.objects.at(*world.player).pose);
                std::vector<std::uint32_t> pixels(indexed.pixels.size());std::transform(indexed.pixels.begin(),indexed.pixels.end(),pixels.begin(),[&](auto c){return scene.palette()[c];});
                if(w==320)compare(pixels,w,h,"PKR scene");
                std::vector<double> cpu,gpu,transform;
                for(bool accelerated:{false,true})for(int i=0;i<14;++i){
                    pixels[0]^=0x010101;gl.begin();const auto t0=Clock::now();
                    if(accelerated)gl.image(pixels,w,h,{0,0,w*3,h*3},true,3);
                    else{const auto scaled=dok::xbr3(pixels,w,h);const auto t1=Clock::now();if(i>=2)transform.push_back(ms(t0,t1));gl.image(scaled,w*3,h*3,{0,0,w*3,h*3},true,0);}
                    finish();if(i>=2)(accelerated?gpu:cpu).push_back(ms(t0,Clock::now()));
                }
                std::cout<<w<<'x'<<h<<" CPU transform "<<median(transform)<<" ms; CPU transform+upload "<<median(cpu)<<" ms; GPU upload+filter "<<median(gpu)<<" ms\n";
                if(!first)report<<',';first=false;report<<"{\"width\":"<<w<<",\"height\":"<<h<<",\"cpu_transform_p50_ms\":"<<median(transform)<<",\"cpu_total_p50_ms\":"<<median(cpu)<<",\"gpu_total_p50_ms\":"<<median(gpu)<<'}';
                check(median(gpu)<median(cpu),"GPU path must be faster than CPU reference on this host");
            }report<<"]}\n";
            gl.frame_size(320,240);gl.begin();gl.image(pattern,13,11,{4,7,39,33},true,3);gl.image({0xff00ff00},1,1,{100,100,10,10},true);const auto composed=gl.read_pixels();
            check(composed[std::size_t(210)*960+370]==0xff00ff00,"xBR failed to restore fullscreen framebuffer/UI state");
        }SDL_DestroyWindow(window);
        dok::VideoSettings software;software.renderer=dok::RendererKind::software;software.width=960;software.height=480;
        {dok::Display display(software,false);check(!display.gl()&&display.gpu_presentation(),"Software world must remain CPU with optional GL presentation");display.begin();display.pixels({0xffffffff,0xff000000,0xff000000,0xffffffff},2,2,false,3);check(display.gpu_xbr_ready(),"Software xBR did not reach GPU");}
        SDL_SetHint("DOK_SOFTWARE_SDL_PRESENTATION","1");
        {dok::Display fallback(software,false);check(!fallback.gl()&&!fallback.gpu_presentation(),"Explicit SDL software presentation fallback");fallback.begin();fallback.pixels({0xffffffff},1,1,false,3);const auto frame=fallback.read_pixels();check(frame[240*960+480]==0xffffffff,"SDL CPU fallback output");}
        SDL_Quit();return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';SDL_Quit();return 1;}
}
