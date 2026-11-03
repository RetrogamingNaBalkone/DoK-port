#pragma once
#include "world.hpp"
#include "raster.hpp"
#include <functional>

namespace dok {
class Weapons {
public:
    Weapons(World& world,const Archive& archive);
    void picked_up(OwnerId weapon);
    void grant_all();
    bool select(int slot);
    bool cycle(int direction);
    void event(OwnerId owner,GameEvent event);
    // Spawn returns the original charge cost, zero on allocation failure.
    // Readiness is consumed even if spawning fails, as in the DOS handler.
    bool fire(const std::function<std::int16_t(std::uint8_t)>& spawn);
    void overlay(IndexedSurface& target,int frame_height=200) const;
private:
    void show(OwnerId owner);
    void hide_current();
    const AnimationFrame& frame(OwnerId owner) const;
    World& world_;
    std::map<OwnerId,std::vector<AnimationFrame>> animations_;
};
}
