#ifndef DOK_COLLISION_H
#define DOK_COLLISION_H
#include "dok/movement.h"
/* Borrowed dynamic MMap occupancy, separate from immutable glyph definitions.
 * The single production world installs this at level initialization. */
typedef int (*dok_collision_overlay_fn)(void *,int32_t x,int32_t z);
void dok_collision_overlay(const dok_runtime *,dok_collision_overlay_fn,void *);
/* Ghidra RUS 1098:0448/0901, ENG 1030:052c/09e5. Native edge lookup
 * clamps cell indices; these two functions retain that behavior for evidence.
 * Coordinates must be in the supported world range (no int32 overflow). */
int dok_collision_clear(const dok_runtime *world,int32_t x,int32_t z,int16_t radius);
void dok_collision_resolve(const dok_runtime *world,int32_t *x,int32_t *z,
    int32_t desired_x,int32_t desired_z);
/* Per-quarter-step movement adapter. Own glyph is temporarily removed, door
 * occupancy is balanced, and out-of-world/corrupt results are rejected.
 * Actor must belong to world; no allocation, no vertical integration. */
void dok_collision_move(dok_runtime_actor *actor,int16_t distance,int strafe,void *world);
/* Direct small displacement for class12 conveyor fields; same resolver and
 * occupancy effects as actor native +10A4. Oversized requests are rejected. */
void dok_collision_displace(dok_runtime *world,dok_runtime_actor *actor,int16_t dx,int16_t dz);
#endif
