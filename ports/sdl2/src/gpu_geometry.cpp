#include "dok/gpu_geometry.hpp"
#include <cmath>
#include <algorithm>
namespace dok {
GpuCamera::GpuCamera(Pose pose,int width,float yaw_remainder,std::array<double,4> offset):pose_(pose),offset_(offset),width_(width){
    const double angle=(pose.heading-yaw_remainder+offset_[3])*3.14159265358979323846/180.0;
    sine_=std::sin(angle);cosine_=std::cos(angle);
}
GpuCamera::Point GpuCamera::transform(double x,double y,double z,double u,double v)const{
    const double dx=x-pose_.x-offset_[0],dz=z-pose_.z-offset_[2];
    return {dx*cosine_+dz*sine_,y-pose_.y-offset_[1],dz*cosine_-dx*sine_,u,v};
}
std::optional<RenderPolygon> GpuCamera::project(const std::array<Point,4>& points,std::uint8_t flags,RasterAxis axis)const{
    // Clip in camera space, interpolating texture coordinates before projection.
    std::array<Point,8> clipped;std::size_t count=0;
    for(std::size_t i=0;i<points.size();++i){const auto& a=points[i];const auto& b=points[(i+1)%points.size()];
        if(a.z>=0)clipped[count++]=a;
        if((a.z<0)!=(b.z<0)){const double t=-a.z/(b.z-a.z);
            clipped[count++]={a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,0,a.u+(b.u-a.u)*t,a.v+(b.v-a.v)*t};}
    }
    if(count<3)return std::nullopt;
    RenderPolygon out;out.flags=flags|1;out.axis=axis;out.updates_image_winding=false;
    out.vertices.reserve(count);
    for(std::size_t i=0;i<count;++i){const auto& p=clipped[i];const double scale=200.0/(p.z+200.0);
        const float x=float(width_*0.5-0.5+p.x*scale),y=float(79.5+p.y*scale);
        RasterVertex vertex{int(std::floor(x)),int(std::floor(y)),int(p.z),int(p.u),int(p.v)};
        vertex.precise=std::array<float,5>{x,y,float(p.z),float(p.u),float(p.v)};
        out.vertices.push_back(vertex);
        out.sort_key+=int(p.z/count);
    }
    return out;
}
std::optional<RenderPolygon> GpuCamera::vertical(Pose a,Pose b,int height,int tw,int th,
    std::uint8_t flags,int v0,int v1,bool reflection)const{
    if(!v1)v1=th-1;
    const double u0=reflection?tw-1:0,u1=reflection?0:tw-1;
    return project({transform(a.x,a.y-height,a.z,u0,v0),transform(b.x,b.y-height,b.z,u1,v0),
        transform(b.x,b.y,b.z,u1,v1),transform(a.x,a.y,a.z,u0,v1)},flags,RasterAxis::vertical);
}
std::optional<RenderPolygon> GpuCamera::sprite(Pose origin,int width,int height,int tw,int th,std::uint8_t flags)const{
    const auto p=transform(origin.x,origin.y,origin.z,0,0);
    if(p.z<=0)return std::nullopt;
    return project({{{p.x-width*0.5,p.y-height,p.z,0,0},{p.x+width*0.5,p.y-height,p.z,double(tw-1),0},
        {p.x+width*0.5,p.y,p.z,double(tw-1),double(th-1)},{p.x-width*0.5,p.y,p.z,0,double(th-1)}}},flags,RasterAxis::vertical);
}
std::optional<RenderPolygon> GpuCamera::horizontal(Pose origin,int side,int tw,int th,bool ceiling)const{
    const double x=origin.x,y=origin.y,z=origin.z;
    if(ceiling)return project({transform(x,y,z,0,0),transform(x+side,y,z,tw-1,0),
        transform(x+side,y,z+side,tw-1,th-1),transform(x,y,z+side,0,th-1)},0x19,RasterAxis::horizontal);
    return project({transform(x,y,z+side,0,0),transform(x+side,y,z+side,tw-1,0),
        transform(x+side,y,z,tw-1,th-1),transform(x,y,z,0,th-1)},0x19,RasterAxis::horizontal);
}
}
