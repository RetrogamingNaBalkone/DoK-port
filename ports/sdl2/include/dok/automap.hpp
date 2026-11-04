#pragma once
#include "world.hpp"
#include "raster.hpp"
namespace dok {
// Native indexed 320x200 map. Old Gold is opaque only inside its 2px grid.
IndexedSurface native_automap(const World& world);
}
