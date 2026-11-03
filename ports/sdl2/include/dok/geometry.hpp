#pragma once
#include "raster.hpp"
#include <optional>

namespace dok {
struct RenderPolygon {
    std::vector<RasterVertex> vertices;
    std::int32_t sort_key{};
    std::uint8_t flags=1;
    bool reverse_edges=false;
    bool updates_image_winding=true;
    RasterAxis axis=RasterAxis::vertical;
    std::size_t material=0,secondary_material=0;
    std::vector<RasterVertex> secondary;
};
// Explicit endpoints avoid conflating cell-edge and cell-centred orientations.
std::optional<RenderPolygon> vertical_plane(const SoftwareCamera& camera,
    Pose a,Pose b,std::int16_t height,int texture_width,int texture_height,
    std::uint8_t flags=1,std::int16_t sort_bias=0,int v_start=0,int v_end=0,
    bool constructor_reflection=false);
std::optional<RenderPolygon> billboard(const SoftwareCamera& camera,Pose origin,
    std::int16_t width,std::int16_t height,int texture_width,int texture_height,
    std::uint8_t flags=1);
void sort_and_cull(std::vector<RenderPolygon>& polygons,ClipRect clip);
std::optional<RenderPolygon> horizontal_plane(const SoftwareCamera& camera,Pose origin,
    std::int16_t side,int texture_width,int texture_height,bool ceiling=false);
} // namespace dok
