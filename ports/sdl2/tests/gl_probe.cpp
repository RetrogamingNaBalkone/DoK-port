#include "dok/gl_renderer.hpp"
#include <SDL_opengl.h>
#include <array>
#include <iostream>
#include <stdexcept>

static void check(bool okay,const char* message){if(!okay)throw std::runtime_error(message);}
int main(){
    SDL_SetMainReady();
    if(SDL_Init(SDL_INIT_VIDEO))return 1;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,0);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,24);
    auto* window=SDL_CreateWindow("GL regression",0,0,640,384,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
    try{
        check(window,"Window creation failed");
        dok::GlRenderer gl(window);
        const auto read=reinterpret_cast<decltype(&glReadPixels)>(SDL_GL_GetProcAddress("glReadPixels"));
        auto sample=[&](int x,int y){std::array<unsigned char,4> p{};
            read(x*2+1,383-int((y+0.5)*384/160),1,1,GL_RGBA,GL_UNSIGNED_BYTE,p.data());return p[0];};
        std::array<std::uint32_t,256> palette{};
        for(unsigned i=0;i<256;++i)palette[i]=0xff000000u|i*0x010101u;
        std::array<std::uint8_t,4096> lut{};
        for(int row=0;row<16;++row)for(int i=1;i<256;++i)lut[row*256+i]=16+row*12;
        const dok::Bytes light{lut.data(),lut.size()};
        const dok::Image image{2,2,0,{1,1,1,1}};
        auto quad=[](int depth){return std::vector<dok::RasterVertex>{{20,20,depth,0,0},{300,20,depth,1,0},{300,140,depth,1,1},{20,140,depth,0,1}};};
        for(auto axis:{dok::RasterAxis::vertical,dok::RasterAxis::horizontal}){
            int previous=-1;
            for(int depth=1920;depth<=3720;depth+=12){
                gl.begin();gl.world(palette);gl.polygon(quad(depth),image,light,axis);
                const int color=sample(160,80);
                check(std::abs(color-(16+(depth-1920)/10.0))<=1,"Smooth LIGHT changed palette anchors");
                if(previous>=0)check(color>=previous&&color-previous<=2,"LIGHT has a visible step");
                previous=color;
            }
        }
        dok::Image sprite=image;sprite.flags=8;
        for(bool wall_first:{false,true}){
            gl.begin();gl.world(palette);
            if(wall_first)gl.polygon(quad(2100),image,light,dok::RasterAxis::vertical);
            gl.polygon(quad(4000),sprite,light,dok::RasterAxis::vertical);
            if(!wall_first)gl.polygon(quad(2100),image,light,dok::RasterAxis::vertical);
            check(sample(160,80)==34,"Distant sprite shows through opaque wall");
            gl.image(std::vector<std::uint32_t>(4,0xffeeeeeeu),2,2,{0,0,320,240});
            check(sample(160,80)==238,"Depth test hides UI");
        }
        // A discarded transparent texel must not write depth.
        gl.begin();gl.world(palette);
        sprite.pixels={0,0,0,0};
        gl.polygon(quad(100),sprite,light,dok::RasterAxis::vertical);
        gl.polygon(quad(2100),image,light,dok::RasterAxis::vertical);
        check(sample(160,80)==34,"Transparent sprite occludes wall");
        for(int row=0;row<16;++row){lut[row*256+1]=35;lut[row*256+2]=220;}
        const dok::Image checker{2,2,0,{1,2,1,2}};
        gl.begin();gl.world(palette);gl.polygon(quad(100),checker,light,dok::RasterAxis::vertical);
        check(sample(160,80)==220,"Default nearest filter blurred texels");
        gl.set_linear_filter(true);
        gl.begin();gl.world(palette);gl.polygon(quad(100),checker,light,dok::RasterAxis::vertical);
        check(sample(160,80)>120&&sample(160,80)<140,"Optional linear filter did not interpolate colors");
        gl.set_linear_filter(false);
        gl.begin();gl.world(palette);gl.polygon(quad(100),checker,light,dok::RasterAxis::vertical);
        check(sample(160,80)==220,"Nearest toggle did not restore crisp texels");
        std::cout<<"Smooth LIGHT anchors/ramp, wall occlusion, transparent depth, UI and nearest/linear passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';SDL_DestroyWindow(window);SDL_Quit();return 1;}
    SDL_DestroyWindow(window);SDL_Quit();
}
