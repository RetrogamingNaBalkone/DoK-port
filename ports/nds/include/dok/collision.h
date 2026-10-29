#ifndef DOK_NDS_COLLISION_H
#define DOK_NDS_COLLISION_H
#include "dok/world.h"
#include "dok/motion.h"

/* Borrowed native lookup. known[glyph]==0 blocks rather than dereferencing a
 * missing DOS far pointer. Arrays contain width*length and 256 entries. */
typedef struct dok_collision_view {
    uint16_t width, length;
    int16_t cell_side, min_distance;
    const unsigned char *mmap, *tmap, *known;
    const uint16_t *flags;
    /* Optional N15 borrowed 4096-cell bitset. NULL is exact static N10. */
    const unsigned char *projectile_occupied;
} dok_collision_view;
struct dok_collision_context;
typedef struct dok_collision_state {
    dok_initial_world *_world;
    struct dok_collision_context *_active;
    uint16_t flags[256], door_count[256];
    unsigned char known[256];
    uint32_t _tag, _revision, _epoch;
    const unsigned char *_projectile_occupied;
} dok_collision_state;
enum {
    DOK_COLLISION_WORLD_EDGE=1u, DOK_COLLISION_LARGE_STEP=2u,
    DOK_COLLISION_OCCUPIED=4u, DOK_COLLISION_FOREIGN=8u,
    DOK_COLLISION_BAD_REQUEST=16u
};
typedef struct dok_collision_report {
    uint32_t requested, committed, adjusted, rejected, reason_mask;
} dok_collision_report;
typedef struct dok_collision_context {
    dok_collision_state *_state;
    dok_object_ref _actor;
    int32_t _x, _y, _z;
    uint32_t _tag, _ordinal;
    unsigned char _glyph;
    dok_collision_report _report;
} dok_collision_context;
/* Query produces an opaque value for this exclusive context/revision. It has
 * no world effects; do not edit its fields or reuse it after another commit. */
typedef struct dok_collision_plan {
    dok_collision_context *_context;
    uint32_t _revision, _ordinal, _old_cell, _new_cell, _reason, _write;
    int32_t _x, _z, _candidate_x, _candidate_z;
} dok_collision_plan;

/* Exact source lookup/resolver, including outside-map clamp. These functions
 * are oracle boundaries, not bounded actor movement. All failure outputs stay
 * unchanged. Positive cell side; dimensions1..64; radius is source signedWORD. */
dok_status dok_collision_native_clear(const dok_collision_view *view,
    int32_t x, int32_t z, int16_t radius, uint32_t *out);
dok_status dok_collision_native_resolve(const dok_collision_view *view,
    int32_t old_x, int32_t old_z, int32_t new_x, int32_t new_z,
    int32_t *out_x, int32_t *out_z);

/* Initialize unused storage, borrowing RU world and its immutable definition.
 * Maps remain owned by world. Runtime fields start from definition flags and
 * native constructor door_count0. Do not rebind a live/active state. */
dok_status dok_collision_bind(dok_collision_state *state, dok_initial_world *world);
dok_status dok_collision_get_view(const dok_collision_state *state,
    dok_collision_view *out);
/* Controlled N11 boundary, no door events/keys/animation. Full reference is
 * checked. Refused while a motion context owns the world. */
dok_status dok_collision_set_flags(dok_collision_state *state,
    dok_object_ref object, uint16_t flags);
dok_status dok_collision_set_door_count(dok_collision_state *state,
    dok_object_ref object, uint16_t count);
/* Bind/unbind outside an active context; storage outlives the binding. */
dok_status dok_collision_projectiles(dok_collision_state *state,const unsigned char *bits);
/* Admission before N09 update. One context exclusively borrows the world until
 * end; no render lease/world reset/other gameplay mutation may overlap it.
 * It validates scalar quarter-step before any occupancy write. Caller still
 * supplies immutable trig service; move reads only requests, never motion. */
dok_status dok_collision_begin(dok_collision_state *state, dok_object_ref actor,
    const dok_motion_state *motion, uint32_t elapsed, dok_collision_context *context);
dok_status dok_collision_query(dok_collision_context *context,
    const dok_motion_substep *request, dok_collision_plan *out);
dok_status dok_collision_commit(dok_collision_context *context,
    const dok_collision_plan *plan);
/* Cannot fail for admitted N09 requests. A defensive invalid request preserves
 * x/z and world, records BAD_REQUEST, and does not invoke foreign callbacks. */
void dok_collision_move(void *context, const dok_motion_substep *request,
    int32_t *accepted_x, int32_t *accepted_z);
dok_status dok_collision_end(dok_collision_context *context, dok_collision_report *out);
/* Pure bounded presentation admission. Ignore exactly current self MMap cell,
 * not all occurrences of its glyph. No motion, counter, occupancy or event
 * writes. Blocked/outside pose returns OK with *out0. */
dok_status dok_collision_camera_clear(const dok_collision_state *state,
    dok_object_ref actor, int32_t self_x, int32_t self_z,
    int32_t camera_x, int32_t camera_z, uint32_t *out);
/* N11 additive direct-wrapper boundary (native RU1068:10A4/PKE1020:15AE).
 * Exactly one resolver/occupancy transaction. Unlike N09 quarter-step query,
 * this accepts legitimate conveyor displacement >=half a cell (C2=4:350).
 * Existing bounded final-pose and foreign-origin guards remain. Publish only
 * x/z after end; preserve all other motion fields. report unchanged on failed
 * admission. No caller may overlap a render lease or another world mutation. */
dok_status dok_collision_displace(dok_collision_state *state,
    dok_object_ref actor, dok_motion_state *motion,
    int32_t candidate_x, int32_t candidate_z, dok_collision_report *out);
#endif
