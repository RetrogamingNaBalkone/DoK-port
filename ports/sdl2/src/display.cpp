#include "dok/display.hpp"
#include "dok/png.hpp"
#include "dok/presentation.hpp"
#include "dok/filters.hpp"
#include <algorithm>
#include <stdexcept>

namespace dok {
Display::Display(const VideoSettings& s,bool vsync){
    try{
        world_gl_=s.renderer==RendererKind::opengl;
        const bool try_gl=world_gl_||!SDL_GetHintBoolean("DOK_SOFTWARE_SDL_PRESENTATION",SDL_FALSE);
        if(try_gl){
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,2);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,0);
            SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,world_gl_?24:0);
        }
        const auto create_window=[&](bool opengl){return SDL_CreateWindow("OGDoK",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,s.width,s.height,
            SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI|SDL_WINDOW_HIDDEN|(opengl?SDL_WINDOW_OPENGL:0));};
        if(try_gl){
            window_=create_window(true);
            if(window_)try{gl_=std::make_unique<GlRenderer>(window_);if(!vsync)SDL_GL_SetSwapInterval(0);}catch(...){if(world_gl_)throw;}
            if(!gl_){if(world_gl_)throw std::runtime_error(SDL_GetError());if(window_)SDL_DestroyWindow(window_);window_=nullptr;}
        }
        if(!gl_){window_=create_window(false);if(!window_)throw std::runtime_error(SDL_GetError());
            renderer_=SDL_CreateRenderer(window_,-1,SDL_RENDERER_ACCELERATED|(vsync?SDL_RENDERER_PRESENTVSYNC:0));
            if(!renderer_)renderer_=SDL_CreateRenderer(window_,-1,SDL_RENDERER_SOFTWARE);if(!renderer_)throw std::runtime_error(SDL_GetError());}
        if(world_gl_&&s.fullscreen){gl_->frame_size(s.fullscreen_width,s.fullscreen_height);frame_width_=s.fullscreen_width;frame_height_=s.fullscreen_height;}
        if(s.fullscreen&&SDL_SetWindowFullscreen(window_,SDL_WINDOW_FULLSCREEN_DESKTOP)!=0)throw std::runtime_error(SDL_GetError());
    }catch(...){gl_.reset();if(renderer_)SDL_DestroyRenderer(renderer_);if(window_)SDL_DestroyWindow(window_);throw;}
}
Display::~Display(){gl_.reset();if(texture_)SDL_DestroyTexture(texture_);if(sharp_texture_)SDL_DestroyTexture(sharp_texture_);for(const auto& overlay:overlay_textures_)SDL_DestroyTexture(overlay.second);if(renderer_)SDL_DestroyRenderer(renderer_);if(window_)SDL_DestroyWindow(window_);}
std::array<int,2> Display::size()const{if(frame_width_)return {frame_width_,frame_height_};int w,h;if(gl_)SDL_GL_GetDrawableSize(window_,&w,&h);else SDL_GetRendererOutputSize(renderer_,&w,&h);return {std::max(1,w),std::max(1,h)};}
bool Display::menu_event(SDL_Event& event)const{
    if(event.type!=SDL_MOUSEMOTION&&event.type!=SDL_MOUSEBUTTONDOWN&&event.type!=SDL_MOUSEBUTTONUP)return true;
    int ww=1,wh=1;SDL_GetWindowSize(window_,&ww,&wh);
    int dw=1,dh=1;if(gl_)SDL_GL_GetDrawableSize(window_,&dw,&dh);else SDL_GetRendererOutputSize(renderer_,&dw,&dh);
    const auto size=this->size();const auto rect=fit_frame(dw,dh,size[0],size[1]);
    const int x=(event.type==SDL_MOUSEMOTION?event.motion.x:event.button.x)*dw/std::max(1,ww);
    const int y=(event.type==SDL_MOUSEMOTION?event.motion.y:event.button.y)*dh/std::max(1,wh);
    if(x<rect.x||y<rect.y||x>=rect.x+rect.w||y>=rect.y+rect.h)return false;
    const int mx=(x-rect.x)*size[0]/rect.w,my=(y-rect.y)*size[1]/rect.h;
    if(event.type==SDL_MOUSEMOTION){event.motion.x=mx;event.motion.y=my;}
    else{event.button.x=mx;event.button.y=my;}return true;
}
void Display::begin(){if(gl_)gl_->begin();else{SDL_SetRenderDrawColor(renderer_,0,0,0,255);SDL_RenderClear(renderer_);}}
void Display::pixels(const std::vector<std::uint32_t>& data,int width,int height,bool game,int filter,const SDL_Rect* target){
    const auto output=size();SDL_Rect r=target?*target:SDL_Rect{0,0,output[0],output[1]};
    if(game){r.w=std::min(output[0],output[1]*4/3);r.h=r.w*3/4;r.x=(output[0]-r.w)/2;r.y=(output[1]-r.h)/2;}
    if(gl_){gl_->image(data,width,height,r,true,filter);return;}
    if(filter==3){if(r.w>width||r.h>height){const auto scaled=xbr3(data,width,height);pixels(scaled,width*3,height*3,false,0,&r);return;}filter=0;}
    if(!texture_||texture_width_!=width||texture_height_!=height){if(texture_)SDL_DestroyTexture(texture_);
        texture_=SDL_CreateTexture(renderer_,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,width,height);texture_width_=width;texture_height_=height;
        if(!texture_)throw std::runtime_error(SDL_GetError());}
    SDL_SetTextureScaleMode(texture_,filter==1?SDL_ScaleModeLinear:SDL_ScaleModeNearest);
    if(SDL_UpdateTexture(texture_,nullptr,data.data(),width*4)!=0)throw std::runtime_error(SDL_GetError());
    SDL_Texture* source=texture_;
    if(filter==2){
        // Nearest integer prescale, then linear presentation. This leaves texel
        // interiors sharp and confines interpolation to one output-pixel edge.
        const int sx=std::max(1,r.w/width),sy=std::max(1,r.h/height);
        const int sw=width*sx,sh=height*sy;
        if(!sharp_texture_||sharp_width_!=sw||sharp_height_!=sh){
            if(sharp_texture_)SDL_DestroyTexture(sharp_texture_);
            sharp_texture_=SDL_CreateTexture(renderer_,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_TARGET,sw,sh);
            sharp_width_=sw;sharp_height_=sh;
            if(!sharp_texture_)throw std::runtime_error(SDL_GetError());
        }
        if(SDL_SetRenderTarget(renderer_,sharp_texture_)!=0)throw std::runtime_error(SDL_GetError());
        const int copied=SDL_RenderCopy(renderer_,texture_,nullptr,nullptr);
        const int restored=SDL_SetRenderTarget(renderer_,nullptr);
        if(copied!=0||restored!=0)throw std::runtime_error(SDL_GetError());
        SDL_SetTextureScaleMode(sharp_texture_,SDL_ScaleModeLinear);source=sharp_texture_;
    }
    if(SDL_RenderCopy(renderer_,source,nullptr,&r)!=0)throw std::runtime_error(SDL_GetError());
}
void Display::present(){if(gl_)gl_->present();else SDL_RenderPresent(renderer_);}
void Display::overlay(const std::vector<std::uint32_t>& pixels,int w,int h,SDL_Rect r){
    if(gl_){gl_->image(pixels,w,h,r,true);return;}
    // Weapon, HUD and FPS layers coexist every frame. Retain each shape instead
    // of reallocating a texture for every consecutive layer.
    const auto key=(std::uint64_t(w)<<32)|unsigned(h);
    if(overlay_textures_.size()>16){for(const auto& entry:overlay_textures_)SDL_DestroyTexture(entry.second);overlay_textures_.clear();}
    auto& texture=overlay_textures_[key];
    if(!texture){
        texture=SDL_CreateTexture(renderer_,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,w,h);
        if(!texture)throw std::runtime_error(SDL_GetError());
        SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_BLEND);SDL_SetTextureScaleMode(texture,SDL_ScaleModeNearest);
    }
    if(SDL_UpdateTexture(texture,nullptr,pixels.data(),w*4)!=0||SDL_RenderCopy(renderer_,texture,nullptr,&r)!=0)throw std::runtime_error(SDL_GetError());
}
std::vector<std::uint32_t> Display::read_pixels(){if(gl_)return gl_->read_pixels();const auto output=size();std::vector<std::uint32_t> data(std::size_t(output[0])*output[1]);if(SDL_RenderReadPixels(renderer_,nullptr,SDL_PIXELFORMAT_ARGB8888,data.data(),output[0]*4)!=0)throw std::runtime_error(SDL_GetError());return data;}
void Display::screenshot(const std::string& path){
    if(gl_){gl_->screenshot(path);return;}const auto output=size();std::vector<std::uint32_t> data(std::size_t(output[0])*output[1]);
    if(SDL_RenderReadPixels(renderer_,nullptr,SDL_PIXELFORMAT_ARGB8888,data.data(),output[0]*4)!=0)throw std::runtime_error(SDL_GetError());
    if(std::filesystem::u8path(path).extension()==".png")save_png(std::filesystem::u8path(path),output[0],output[1],data);
    else{auto* surface=SDL_CreateRGBSurfaceWithFormatFrom(data.data(),output[0],output[1],32,output[0]*4,SDL_PIXELFORMAT_ARGB8888);
        if(!surface)throw std::runtime_error(SDL_GetError());const int error=SDL_SaveBMP(surface,path.c_str());SDL_FreeSurface(surface);if(error)throw std::runtime_error(SDL_GetError());}
}
}
