#pragma once
#include "world.hpp"
#include "scene.hpp"

namespace dok {
// Owns immutable decoded clips; all mutable playback state belongs to World.
// Renderer reads image() without activating or advancing the simulation.
class Animations {
public:
    Animations(World& world,const Archive& archive);
    void consider(CellBounds bounds);
    bool activate(OwnerId owner);
    void event(OwnerId owner);
    const Image* image(OwnerId owner) const;
private:
    void select(WorldObject& object,std::size_t frame);
    World& world_;
    std::map<OwnerId,std::vector<AnimationFrame>> clips_;
};
}
