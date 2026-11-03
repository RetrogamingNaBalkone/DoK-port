#pragma once
#include "raster.hpp"
#include <SDL.h>
#include <memory>
namespace dok {
class GlRenderer {
public:
    explicit GlRenderer(SDL_Window* window);
    ~GlRenderer();
    void begin();
    int view_width() const;
    void set_linear_filter(bool enabled);
    void world(const std::array<std::uint32_t,256>& palette);
    void polygon(const std::vector<RasterVertex>& vertices,const Image& image,Bytes light,RasterAxis axis);
    void image(const std::vector<std::uint32_t>& pixels,int width,int height,SDL_Rect logical_rect,bool screen_pixels=false);
    void present();
    void screenshot(const std::string& path);
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
