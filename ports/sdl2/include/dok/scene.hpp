#pragma once
#include "geometry.hpp"
#include "projectiles.hpp"
#include <map>

namespace dok {
struct CellBounds {int min_x,min_z,max_x,max_z;};
CellBounds original_cell_bounds(Pose pose,int side,int map_width,int map_length);
CellBounds view_cell_bounds(Pose pose,int side,int map_width,int map_length,int view_width);
// Kremlin class12: original 18-phase cyclic image crop; fixed port cadence.
Image flowing_surface_image(const Image& source,std::uint64_t ticks);
// Initial-state scene. Dynamic image getters/crop and visibility activation are
// connected by subsequent gameplay work; this class does not tick the world.
class SoftwareScene {
public:
    using Draw=std::function<void(const std::vector<RasterVertex>&,const Image&,Bytes,RasterAxis)>;
    SoftwareScene(const Archive& archive,const LevelDefinition& level);
    const IndexedSurface& render(const World& world,Pose pose,const std::vector<PositionedImage>& dynamic={},
        const Draw& gpu={},int view_width=320,std::array<double,4> camera_offset={});
    const std::array<std::uint32_t,256>& palette() const noexcept {return palette_;}
    std::size_t polygon_count() const noexcept {return polygon_count_;}
private:
    DatasetId dataset_=DatasetId::unknown;
    std::string level_name_;
    std::size_t resource(const std::string& name) const;
    std::map<std::string,std::size_t> names_;
    std::vector<Image> images_;
    std::vector<std::size_t> object_images_;
    std::map<std::size_t,Image> door_sources_;
    std::map<std::size_t,Image> flowing_sources_;
    std::vector<std::uint8_t> light_;
    std::array<std::uint32_t,256> palette_{};
    IndexedSurface surface_{320,160};
    std::size_t polygon_count_=0;
};
} // namespace dok
