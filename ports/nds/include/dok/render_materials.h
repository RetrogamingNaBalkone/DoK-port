#ifndef DOK_RENDER_MATERIALS_H
#define DOK_RENDER_MATERIALS_H
#include "dok/balance.h"
#include "dok/geometry.h"

#define DOK_RENDER_MATERIALS 128u
#define DOK_RENDER_SOURCE_BYTES 65536u

typedef struct dok_render_material {
    dok_asset_entry entry;
    const unsigned char *pixels;
    uint32_t color0_transparent;
} dok_render_material;
typedef struct dok_render_world {
    dok_level_definition base, effective;
    dok_initial_world initial;
    dok_level_presentation presentation;
    dok_balance_diff diff;
    dok_geometry_binding bindings[256];
    dok_render_material materials[DOK_RENDER_MATERIALS];
    uint32_t material_count, asset_bytes;
} dok_render_world;
_Static_assert(sizeof(dok_render_world)<=DOK_NDS_WORLD_BYTES,
               "N07 resident world and bindings fit N03 world region");
typedef union dok_render_workspace {
    dok_world_workspace world;
    dok_level_presentation presentation;
} dok_render_workspace;
_Static_assert(sizeof(dok_render_workspace)<=DOK_NDS_SCRATCH_BYTES,
               "N07 typed transient workspace fits N03 scratch region");

/* Explicit loading boundary only. All destinations are unpublished storage;
 * bytes may change on failure. Asset pool starts with a pinned 64KiB LEVEL
 * source slot, then aligned resident GPU texture payloads. No per-frame I/O.
 * Uses immutable accepted N04/N05 modules without changing their contracts. */
dok_status dok_render_world_load(const dok_asset_session *session,
    uint32_t level, dok_world_epoch epoch, dok_render_world *world,
    dok_render_workspace *scratch, unsigned char *asset_pool,
    uint32_t pool_capacity);
#endif
