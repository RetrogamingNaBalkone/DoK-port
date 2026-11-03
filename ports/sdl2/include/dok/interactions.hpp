#pragma once
#include "world.hpp"

namespace dok {
enum class UseResult { ignored, locked, scheduled, queue_full };
UseResult use_door(World& world,OwnerId target,OwnerId source);
UseResult player_use(World& world);
// Entry-triggered class13 FCMap sensors; call before each player movement tick.
void player_sensors(World& world);
void door_event(World& world,OwnerId owner,GameEvent event);
// Native right/down zero-fill modes used by Kremlin doors. Each door owns
// its image header and crop state even when the immutable source is shared.
Image door_image(const Image& source,const DoorState& state);
}
