#pragma once
#include "raster.hpp"
namespace dok {
// Sixteen independently lit RGBA mip pyramids, four by four tiles. Mip levels
// run horizontally within each tile; shader clamps each level before filtering.
struct MipAtlas {int width=0,height=0,tile_width=0;std::vector<std::uint32_t> pixels;};
MipAtlas make_mip_atlas(const Image&,const std::array<std::uint32_t,256>&,Bytes light,bool transparent);
std::vector<std::uint32_t> xbr3(const std::vector<std::uint32_t>&,int width,int height);
}
