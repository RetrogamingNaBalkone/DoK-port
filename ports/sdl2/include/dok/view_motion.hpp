#pragma once
#include "camera.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace dok {
// One player interval of presentation latency; never extrapolate through walls.
// Only tick motion is delayed. Live mouse yaw remains on the current pose.
class ViewMotion {
public:
    void reset() { delta_={}; }
    template<class Update> void step(Pose& pose,double tick,Update update) {
        const auto before=pose;
        update();
        record(before,pose,tick);
    }
    void record(Pose before,Pose after,double tick) {
        tick_=tick;
        delta_={double(before.x)-after.x,double(before.y)-after.y,
                double(before.z)-after.z,std::remainder(double(before.heading)-after.heading,360.0)};
    }
    std::array<double,4> offset(double now) const {
        auto result=delta_;
        const double remaining=1.0-std::clamp((now-tick_)/416.0,0.0,1.0);
        for(auto& value:result)value*=remaining;
        return result;
    }
private:
    std::array<double,4> delta_{};
    double tick_=0;
};
}
