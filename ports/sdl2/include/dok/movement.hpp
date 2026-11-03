#pragma once
#include "world.hpp"
#include "contracts.hpp"

namespace dok {
bool footprint_clear(const World& world,std::int32_t x,std::int32_t z);
Pose resolve_movement(const World& world,Pose from,std::int32_t candidate_x,std::int32_t candidate_z);
void move_actor(World& world,OwnerId actor,std::int16_t distance,bool strafe);
void move_actor_to(World& world,OwnerId actor,std::int32_t x,std::int32_t z);
void update_player_motion(World& world,const InputActions& input,std::uint32_t now);
void update_actor_motion(World& world,OwnerId actor,const InputActions& input,std::uint32_t now);
void apply_mouse_yaw(World& world,float degrees);
} // namespace dok
