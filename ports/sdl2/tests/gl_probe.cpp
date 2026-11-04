#include "dok/gl_renderer.hpp"
#include <SDL_opengl.h>
#include <algorithm>
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
        // A reused allocation is not a content identity, including within a frame.
        dok::Image changing{2,2,0,{1,1,1,1}};
        gl.begin();gl.world(palette);
        gl.polygon(quad(200),changing,light,dok::RasterAxis::vertical);
        check(sample(160,80)==35,"Initial cached texture");
        std::fill(changing.pixels.begin(),changing.pixels.end(),2);
        gl.polygon(quad(100),changing,light,dok::RasterAxis::vertical);
        check(sample(160,80)==220,"In-place texture mutation reused stale hash");
        changing.width=1;changing.height=4;
        auto narrow=quad(50);for(auto& v:narrow)v.u=0;
        gl.polygon(narrow,changing,light,dok::RasterAxis::vertical);
        check(sample(160,80)==220,"Reshaped texture cache");
        std::fill(changing.pixels.begin(),changing.pixels.end(),1);
        gl.begin();gl.world(palette);gl.polygon(narrow,changing,light,dok::RasterAxis::vertical);
        check(sample(160,80)==35,"Cross-frame texture mutation reused stale hash");
        for(bool linear:{false,true}){
            gl.set_linear_filter(linear);
            auto frame=[&](bool deferred){
                gl.begin();gl.world(palette,deferred);
                gl.polygon(quad(2100),checker,light,dok::RasterAxis::horizontal);
                gl.polygon(quad(100),sprite,light,dok::RasterAxis::vertical);
                gl.polygon(quad(2200),image,light,dok::RasterAxis::vertical);
                gl.polygon(quad(2100),changing,light,dok::RasterAxis::vertical);
                gl.flush_world();
                std::vector<unsigned char> pixels(640*384*4);
                read(0,0,640,384,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());return pixels;
            };
            check(frame(false)==frame(true),"Deferred VBO changed polygon order or state");
        }
        gl.set_linear_filter(false);palette[35]=0xff4d4d4d;
        gl.begin();gl.world(palette);gl.polygon(narrow,changing,light,dok::RasterAxis::vertical);
        check(sample(160,80)==77,"Palette change did not invalidate composed LIGHT");
        for(int value:{2,1,2}){
            std::fill(changing.pixels.begin(),changing.pixels.end(),value);
            gl.begin();gl.world(palette,true,true);
            gl.polygon(narrow,changing,light,dok::RasterAxis::vertical);
            gl.flush_world();check(sample(160,80)==(value==1?77:220),"Immutable pass reused previous frame pixels");
        }
        std::vector<dok::Image> materials;
        for(int i=1;i<=10;++i){materials.push_back({1,1,0,{static_cast<std::uint8_t>(i)}});
            for(int row=0;row<16;++row)lut[row*256+i]=i*17;}
        for(bool mixed:{false,true})for(bool linear:{false,true}){
            for(std::size_t i=0;i<materials.size();++i)materials[i].flags=mixed&&i%3==0?8:0;
            gl.set_linear_filter(linear);
            auto many=[&](bool deferred){
                gl.begin();gl.world(palette,deferred,true);
                for(int i=0;i<10;++i){auto vertices=quad(100);
                    vertices[0].x=vertices[3].x=10+i*28;vertices[1].x=vertices[2].x=44+i*28;
                    gl.polygon(vertices,materials[i],light,dok::RasterAxis::vertical);
                }
                gl.flush_world();std::vector<unsigned char> pixels(640*384*4);
                read(0,0,640,384,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());return pixels;
            };
            check(many(false)==many(true),"Seven-material batch rollover/blend changed pixels");
        }
        std::cout<<"Smooth LIGHT anchors/ramp, wall occlusion, transparent depth, UI and nearest/linear passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';SDL_DestroyWindow(window);SDL_Quit();return 1;}
    SDL_DestroyWindow(window);SDL_Quit();
}
