#pragma once
#include "camera.hpp"
#include "arithmetic.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace dok {
struct PlayerView {
    Pose pose;
    std::array<double,4> offset;
};
// Stabilize only presentation: OG projectile height and saved motion stay native.
inline PlayerView player_view(const World& world,bool bobbing,std::array<double,4> offset={}) {
    const auto& actor=world.objects.at(*world.player);
    PlayerView view{*actor.pose,offset};
    if(!bobbing&&world.definition.dataset==DatasetId::old_gold){
        view.pose.y=original::negate16(world.definition.objects.at(actor.definition_index).height_parameter);
        view.offset[1]=0;
    }
    return view;
}
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
