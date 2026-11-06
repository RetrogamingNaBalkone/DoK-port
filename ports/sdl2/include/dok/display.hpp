#pragma once
#include "settings.hpp"
#include "gl_renderer.hpp"
#include <memory>
#include <unordered_map>
namespace dok {
// Owns presentation resources only; replacing it never owns or resets the world.
class Display {
public:
    explicit Display(const VideoSettings& settings,bool vsync=true);
    ~Display();
    Display(const Display&)=delete;
    Display& operator=(const Display&)=delete;
    SDL_Window* window() const {return window_;}
    // A software world can still use GL solely to present its CPU framebuffer.
    GlRenderer* gl() const {return world_gl_?gl_.get():nullptr;}
    bool gpu_presentation()const{return gl_!=nullptr;}
    bool gpu_xbr_ready()const{return gl_&&gl_->xbr_gpu_ready();}
    void begin();
    void pixels(const std::vector<std::uint32_t>& pixels,int width,int height,bool game,int filter=0,const SDL_Rect* target=nullptr);
    void overlay(const std::vector<std::uint32_t>& pixels,int width,int height,SDL_Rect target);
    void present();
    std::vector<std::uint32_t> read_pixels();
    void screenshot(const std::string& path);
    std::array<int,2> size()const;
    bool menu_event(SDL_Event& event)const;
private:
    int frame_width_=0,frame_height_=0;
    bool world_gl_=false;
    SDL_Window* window_=nullptr;
    SDL_Renderer* renderer_=nullptr;
    SDL_Texture* texture_=nullptr;
    std::unordered_map<std::uint64_t,SDL_Texture*> overlay_textures_;
    SDL_Texture* sharp_texture_=nullptr;
    int sharp_width_=0,sharp_height_=0;
    int texture_width_=0,texture_height_=0;
    std::unique_ptr<GlRenderer> gl_;
};
}
