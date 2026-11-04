#pragma once
#include "geometry.hpp"
namespace dok {
// Presentation-only camera. Never writes simulation poses or visibility.
class GpuCamera {
public:
    GpuCamera(Pose pose,int width,float yaw_remainder=0,std::array<double,4> offset={});
    std::optional<RenderPolygon> vertical(Pose a,Pose b,int height,int tw,int th,
        std::uint8_t flags,int v0=0,int v1=0,bool reflection=false) const;
    std::optional<RenderPolygon> sprite(Pose origin,int width,int height,int tw,int th,std::uint8_t flags) const;
    std::optional<RenderPolygon> horizontal(Pose origin,int side,int tw,int th,bool ceiling) const;
private:
    struct Point {double x,y,z,u,v;};
    Point transform(double x,double y,double z,double u,double v) const;
    std::optional<RenderPolygon> project(const std::array<Point,4>& points,std::uint8_t flags,RasterAxis axis) const;
    Pose pose_;std::array<double,4> offset_;int width_;double sine_,cosine_;
};
}
