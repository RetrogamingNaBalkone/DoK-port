#pragma once
#include "world.hpp"

namespace dok {
struct PositionedImage {Pose pose;const Image* image=nullptr;std::int16_t scale=1;std::uint16_t flags=0;OwnerId replaces=0;};
class Projectiles {
public:
    std::function<bool(OwnerId,std::int16_t)> monster_hit;
    bool boundary_hits=false; // Optional adaptation; native contact remains the default.
    Projectiles(World& world,const Archive& archive);
    std::int16_t spawn(OwnerId source,std::uint8_t template_glyph);
    void event(OwnerId owner);
    std::vector<PositionedImage> images() const;
    std::size_t active() const;
    static constexpr OwnerId owner_base=0x10000;
private:
    struct Clips {std::vector<AnimationFrame> flight,hit,miss;};
    std::size_t cell(Pose pose) const;
    void unoccupy(unsigned slot);
    void occupy(unsigned slot);
    bool hit(WorldObject& target,const ProjectileState& projectile);
    bool blocks_terrain(const WorldObject& target) const;
    bool clear_to_monster(Pose from,Pose to) const;
    WorldObject* nearby_hit(const ProjectileState& projectile,Pose from);
    const AnimationFrame& frame(const ProjectileState& projectile) const;
    World& world_;
    std::map<std::size_t,Clips> clips_;
};
}
