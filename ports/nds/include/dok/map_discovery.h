#ifndef DOK_MAP_DISCOVERY_H
#define DOK_MAP_DISCOVERY_H
#include "dok/sprites.h"
#include "dok/geometry_prepared.h"

#define DOK_MAP_DISCOVERY_RECORDS (DOK_RENDER_QUADS+DOK_SPRITE_LIMIT)
/* Logical replacement of original far render record. +4 native cell index,
 * +6 byte flags, +7 signed DWORD key; horizontal bounds are signed WORDs. */
typedef struct dok_map_render_record {
    int32_t key;
    int16_t left, right;
    uint16_t cell, flags;
} dok_map_render_record;
typedef struct dok_map_projection_factor {
    int32_t denominator, factor;
} dok_map_projection_factor;
typedef struct dok_map_projection_point {
    int32_t x,z,depth,lateral;
} dok_map_projection_point;
typedef struct dok_map_discovery_workspace {
    dok_map_render_record records[DOK_MAP_DISCOVERY_RECORDS];
    uint16_t order[DOK_MAP_DISCOVERY_RECORDS];
    unsigned char columns[320];
    uint32_t count;
    /* N21P transient lookup only. Reverse insertion preserves the original
     * sprite-list order inside each cell; no persistent world/mesh cache. */
    uint16_t sprite_heads[DOK_LEVEL_MAX_CELLS], sprite_next[DOK_SPRITE_LIMIT];
    dok_map_projection_factor factors[256];
    dok_geometry_field54 field54[DOK_LEVEL_MAX_OBJECTS];
    /* N23 optional, exact-key transient transforms for this update's camera. */
    dok_map_projection_point points[128];
    uint32_t point_valid[4];
} dok_map_discovery_workspace;
typedef struct dok_map_discovery_stats {
    uint32_t records, eligible, occluded, newly_visited;
} dok_map_discovery_stats;
_Static_assert(sizeof(dok_map_discovery_workspace)<=DOK_NDS_SCRATCH_BYTES,
    "N20 transient records fit shared scratch");

/* Original-space projection, before DS guard band/GPU clipping. Rejected
 * records return OK with flags bit0 clear. out unchanged on argument error. */
dok_status dok_map_record_geometry(const dok_geometry_quad *,
    const dok_nds_render_camera *,dok_map_render_record *out);
dok_status dok_map_record_sprite(const dok_sprite_pose *,
    const dok_nds_render_camera *,dok_map_render_record *out);
/* Exact original gap sort and nearest-to-farthest open-column pass. Records
 * must already be native eligible constructor outputs in source order.
 * No mutation of visited until validation succeeds. */
dok_status dok_map_discovery_records(dok_map_discovery_workspace *,
    unsigned char visited[DOK_LEVEL_MAX_CELLS],dok_map_discovery_stats *out);
/* Native gather bounds, including the original overwritten-p1 maximum slot.
 * This is intentionally separate from the conservative N07 visual gather. */
dok_status dok_map_discovery_bounds(const dok_initial_world *,
    const dok_nds_render_camera *,uint32_t heading,dok_render_gather_bounds *out);
/* Uses original gather bounds, constructors and column disclosure.
 * Runtime flags and already gathered sprites
 * describe the same read-only world boundary. Workspace borrows shared scratch
 * only during this call; neither world nor scene is retained or modified.
 * Paired FLOOR/CEILING produces ONE source record. Only N12 world-cell IDs
 * 0x80000000|plane<<24|cell are accepted; projectile/synthetic pool IDs are
 * ignored, correcting the source global-object low-WORD corner disclosure. */
dok_status dok_map_discovery_update(const dok_initial_world *,
    const dok_geometry_binding [256],const dok_sprite_list *,const uint16_t *,
    const dok_nds_render_camera *,uint32_t heading,
    dok_map_discovery_workspace *,unsigned char [DOK_LEVEL_MAX_CELLS],
    dok_map_discovery_stats *out);
#endif
