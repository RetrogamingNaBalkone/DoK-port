#pragma once
#include "world.hpp"

namespace dok {
bool change_player_stat(World& world,std::int16_t category,std::int16_t value);
// Returns the picked object's owner for later sound/text presentation, or zero.
OwnerId player_pickup(World& world);
// Applies class12 surface effect. True means the native slow-movement bit.
bool player_surface(World& world);
}
