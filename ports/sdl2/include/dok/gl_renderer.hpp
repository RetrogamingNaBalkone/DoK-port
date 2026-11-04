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
    // Scene rectangle in drawable pixels; UI always uses the full drawable.
    void scene_view(SDL_Rect rect,int width,int height=160);
    void finish_scene(bool fxaa);
    // immutable_images: pixel buffers remain alive and unchanged throughout the
    // polygon submission sequence. flush_world ends this contract (uses GPU IDs).
    // Scene rendering satisfies this; arbitrary callers keep byte validation per call.
    void world(const std::array<std::uint32_t,256>& palette,bool defer=false,bool immutable_images=false);
    // Deferred polygons preserve submission order; flush before readback/timing.
    void flush_world();
    void polygon(const std::vector<RasterVertex>& vertices,const Image& image,Bytes light,RasterAxis axis);
    void image(const std::vector<std::uint32_t>& pixels,int width,int height,SDL_Rect logical_rect,bool screen_pixels=false,int filter=0);
    void present();
    void screenshot(const std::string& path);
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
