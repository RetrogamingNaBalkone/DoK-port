#pragma once
#include "world.hpp"

namespace dok {
std::int32_t original_sine(int heading);
std::int32_t original_cosine(int heading);
struct CameraParameters {
    std::int16_t width, height_argument, scale, focal_q12;
    std::int32_t distance;
    static CameraParameters kremlin_vga() noexcept { return {320,161,8,4096,200}; }
};
struct CameraPoint { std::int32_t lateral, vertical, depth; };
struct ScreenPoint { std::int32_t x, y, factor; };
struct ClipRect { std::int32_t left,top,right,bottom; }; // Inclusive original bounds.
class SoftwareCamera {
public:
    explicit SoftwareCamera(CameraParameters parameters);
    void set_pose(Pose pose);
    CameraPoint transform(std::int32_t x,std::int32_t y,std::int32_t z) const;
    ScreenPoint project(CameraPoint point) const;
    ClipRect clip() const noexcept { return clip_; }
    std::int16_t center_x() const noexcept { return center_x_; }
    std::int16_t center_y() const noexcept { return center_y_; }
private:
    CameraParameters parameters_;
    Pose pose_{};
    ClipRect clip_{};
    std::int16_t center_x_{},center_y_{};
    std::int32_t sine_=0,cosine_=32768;
};
} // namespace dok
