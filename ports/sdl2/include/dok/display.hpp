#pragma once
#include "settings.hpp"
#include "gl_renderer.hpp"
#include <memory>
namespace dok {
// Owns presentation resources only; replacing it never owns or resets the world.
class Display {
public:
    explicit Display(const VideoSettings& settings);
    ~Display();
    Display(const Display&)=delete;
    Display& operator=(const Display&)=delete;
    SDL_Window* window() const {return window_;}
    GlRenderer* gl() const {return gl_.get();}
    void begin();
    void pixels(const std::vector<std::uint32_t>& pixels,int width,int height,bool game,bool linear=false);
    void overlay(const std::vector<std::uint32_t>& pixels,int width,int height,SDL_Rect target);
    void present();
    void screenshot(const std::string& path);
    std::array<int,2> size()const;
private:
    SDL_Window* window_=nullptr;
    SDL_Renderer* renderer_=nullptr;
    SDL_Texture* texture_=nullptr;
    int texture_width_=0,texture_height_=0;
    std::unique_ptr<GlRenderer> gl_;
};
}
