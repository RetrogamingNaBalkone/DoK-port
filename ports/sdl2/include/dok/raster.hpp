#pragma once
#include "camera.hpp"

namespace dok {
struct RasterVertex {
    std::int32_t x,y,depth,u,v;
    // Optional GPU coordinates; the native raster continues to use WORD/Q16.
    std::optional<std::array<float,5>> precise=std::nullopt;
};
enum class RasterAxis { horizontal, vertical };
class IndexedSurface {
public:
    IndexedSurface(int width,int height);
    int width() const noexcept { return width_; }
    int height() const noexcept { return height_; }
    void clear(std::uint8_t index=0);
    std::vector<std::uint8_t> pixels;
private:
    int width_,height_;
};
// Independent native affine edge/span implementation, not triangle/GPU mapping.
// Crop transforms must be applied to image pixels before this call.
void rasterize(IndexedSurface& target,const std::vector<RasterVertex>& vertices,
               const Image& image,Bytes light,ClipRect clip,RasterAxis axis,
               std::int16_t light_near=1920);
unsigned light_row(std::int32_t depth_q16,std::int16_t near_distance) noexcept;
} // namespace dok
