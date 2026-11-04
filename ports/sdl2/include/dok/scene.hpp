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
// Rendering never ticks or activates the world. Mutable playback is supplied
// through a const image getter before geometry is built.
class SoftwareScene {
public:
    using ImageGetter=std::function<const Image*(OwnerId)>;
    using Draw=std::function<void(const std::vector<RasterVertex>&,const Image&,Bytes,RasterAxis)>;
    SoftwareScene(const Archive& archive,const LevelDefinition& level);
    void software_resolution(int width,int height);
    // Modern viewport: square game coordinates plus 1.2 VGA pixel correction.
    // Native software_resolution remains the independent DOS reference path.
    void software_presentation(int width,int height);
    void base_fov(int degrees){presentation_focal(degrees);base_fov_=degrees;}
    // Explicit diagnostic hooks; never enabled by the gameplay application.
    void native_light_history(bool enabled){if(enabled)raster_reference_.emplace();else raster_reference_.reset();}
    using GeometryObserver=std::function<void(const std::vector<RenderPolygon>&)>;
    void inspect_geometry(GeometryObserver observer){geometry_observer_=std::move(observer);}
    int frame_height() const {return frame_height_;}
    const IndexedSurface& render(const World& world,Pose pose,const std::vector<PositionedImage>& dynamic={},
        const Draw& gpu={},int view_width=320,std::array<double,4> camera_offset={},const ImageGetter& getter={},int gpu_height=160);
    const std::array<std::uint32_t,256>& palette() const noexcept {return palette_;}
    std::size_t polygon_count() const noexcept {return polygon_count_;}
    // Native 320-pixel camera and horizontal column occlusion, independent of
    // the presentation backend/FOV. Does not rasterize or mutate the world.
    std::vector<std::size_t> discovered_cells(const World& world,Pose pose,
        const std::vector<PositionedImage>& dynamic={},const ImageGetter& getter={});
private:
    bool discovering_=false;
    std::vector<std::size_t> discovered_;
    std::optional<RasterReferenceState> raster_reference_;
    GeometryObserver geometry_observer_;
    DatasetId dataset_=DatasetId::unknown;
    std::string level_name_;
    std::size_t resource(const std::string& name) const;
    std::map<std::string,std::size_t> names_;
    std::vector<Image> images_;
    std::vector<std::size_t> object_images_;
    std::vector<const Image*> bound_images_;
    std::map<std::size_t,Image> door_sources_;
    std::map<std::size_t,Image> flowing_sources_;
    std::map<std::size_t,std::pair<int,int>> door_phases_;
    unsigned flowing_phase_=18;
    std::vector<std::uint8_t> light_;
    std::array<std::uint32_t,256> palette_{};
    IndexedSurface surface_{320,160};
    bool surface_dirty_=false;
    CameraParameters software_camera_=CameraParameters::kremlin_vga();
    int frame_height_=200,base_fov_=0,presentation_view_width_=320;
    std::size_t polygon_count_=0;
};
} // namespace dok
