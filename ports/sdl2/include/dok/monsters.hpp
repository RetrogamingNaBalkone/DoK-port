#pragma once
#include "scene.hpp"

namespace dok {
int original_direction(std::int32_t z,std::int32_t x);
class Monsters {
public:
    Monsters(World& world,const Archive& archive,Projectiles& projectiles);
    ~Monsters();
    void consider(CellBounds bounds);
    void wake(OwnerId owner);
    bool damage(OwnerId owner,std::int16_t amount);
    void event(OwnerId owner);
    std::vector<PositionedImage> images(CellBounds bounds) const;
private:
    struct Target {OwnerId owner;unsigned cls;std::uint16_t flags;};
    std::size_t cell(Pose pose) const;
    Target lookup(Pose& pose,int distance,bool terrain) const;
    bool sight(WorldObject& actor) const;
    void display(WorldObject& actor,int clip,int frame);
    World& world_;
    Projectiles& projectiles_;
    std::map<OwnerId,std::array<std::vector<AnimationFrame>,3>> clips_;
};
}
