#include "dok/camera.hpp"
#include "dok/arithmetic.hpp"
#include <stdexcept>
#include <cmath>

namespace dok {
namespace {
#include "trig.inc"
}
using namespace original;
double presentation_focal(int degrees){
    if(!degrees)return 200.0;
    if(degrees<45||degrees>110)throw std::invalid_argument("Base FOV must be original or 45..110 degrees");
    return 160.0/std::tan(degrees*3.14159265358979323846/360.0);
}
std::int32_t original_sine(int heading){if(heading<0||heading>360)throw std::out_of_range("Trig heading");return sine[heading];}
std::int32_t original_cosine(int heading){if(heading<0||heading>360)throw std::out_of_range("Trig heading");return cosine[heading];}
SoftwareCamera::SoftwareCamera(CameraParameters parameters):parameters_(parameters){
    if(parameters.width<=0||parameters.height_argument<=0||parameters.scale<=0)
        throw std::invalid_argument("Invalid software viewport");
    const auto hx=sar32(parameters.width,1),hy=sar32(parameters.height_argument,1);
    const auto ex=sar32(multiply32(parameters.scale,hx),3);
    const auto ey=sar32(multiply32(parameters.scale,hy),3);
    clip_={sub32(hx,ex),sub32(hy,ey),sub32(add32(hx,ex),1),sub32(add32(hy,ey),1)};
    center_x_=signed16(static_cast<std::uint16_t>(add32(sar32(clip_.left,1),sar32(clip_.right,1))));
    center_y_=signed16(static_cast<std::uint16_t>(add32(sar32(clip_.top,1),sar32(clip_.bottom,1))));
}
void SoftwareCamera::set_pose(Pose pose){
    if(pose.heading<0||pose.heading>360)throw std::out_of_range("Original camera heading outside trig table");
    pose_=pose;sine_=sine[pose.heading];cosine_=cosine[pose.heading];
}
CameraPoint SoftwareCamera::transform(std::int32_t x,std::int32_t y,std::int32_t z)const{
    const auto dx=sub32(x,pose_.x),dz=sub32(z,pose_.z);
    return {sar32(add32(multiply32(dx,cosine_),multiply32(dz,sine_)),15),
            sub32(y,pose_.y),
            sar32(sub32(multiply32(dz,cosine_),multiply32(dx,sine_)),15)};
}
ScreenPoint SoftwareCamera::project(CameraPoint point)const{
    const auto numerator=multiply32(multiply32(parameters_.scale,parameters_.distance),parameters_.focal_q12);
    const auto factor=divide32(numerator,add32(point.depth,parameters_.distance));
    return {add32(center_x_,sar32(multiply32(point.lateral,factor),15)),
            add32(center_y_,sar32(multiply32(point.vertical,factor),15)),factor};
}
} // namespace dok
