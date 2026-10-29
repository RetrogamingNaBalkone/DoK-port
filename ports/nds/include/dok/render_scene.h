#ifndef DOK_RENDER_SCENE_H
#define DOK_RENDER_SCENE_H
#include "dok/geometry.h"
#include "dok/render_packet.h"

#define DOK_RENDER_QUADS 1984u
#define DOK_RENDER_CELLS 1984u

typedef struct dok_render_gather_bounds {
    int32_t min_x, min_z, max_x, max_z;
} dok_render_gather_bounds;
typedef struct dok_render_scene_counts {
    uint32_t gathered_cells, source_quads, frustum_rejected, published_quads;
    uint32_t merge_candidates, merged_quads;
    dok_render_gather_bounds bounds;
} dok_render_scene_counts;
typedef struct dok_render_merge_record {
    uint32_t retained_surface_id, consumed_surface_id;
} dok_render_merge_record;

/* One bounded, caller-owned staging/lease. No whole-world mesh is stored.
 * Do not prepare while leased. submit consumes borrowed vertices synchronously;
 * after its return the caller releases before preparing the next snapshot. */
typedef struct dok_render_scene {
    dok_nds_render_quad quads[DOK_RENDER_QUADS];
    dok_nds_render_cell cells[DOK_RENDER_CELLS];
    dok_nds_render_frame frame;
    dok_render_merge_record merges[DOK_RENDER_QUADS];
    dok_render_scene_counts counts;
    uint32_t tag, leased, generation, world_epoch;
} dok_render_scene;
_Static_assert(sizeof(dok_render_scene)<=DOK_NDS_SCENE_BYTES,
               "N07 scene fits N03 scene region on host and ARM9");

typedef enum dok_render_scene_result {
    DOK_RENDER_SCENE_OK=0, DOK_RENDER_SCENE_ARGUMENT=1,
    DOK_RENDER_SCENE_BUSY=2, DOK_RENDER_SCENE_LIMIT=3,
    DOK_RENDER_SCENE_GEOMETRY=4
} dok_render_scene_result;

void dok_render_scene_init(dok_render_scene *scene);
/* Integer heading 0..360. Values come from the original runtime table,
 * verified against the Ghidra-derived trig generator before compilation. */
int dok_render_trig(uint32_t heading, int32_t *sine, int32_t *cosine);
/* Exact adjacent rectangle/UV continuation only, same lattice origin.
 * Repeated logical texture resets are deliberately ineligible. Returns 1 on
 * merge, else 0 with out unchanged. Provenance is retained by scene builder. */
int dok_render_merge(const dok_nds_render_quad *a,const dok_nds_render_quad *b,
    dok_nds_render_quad *out);
/* Conservative superset of the original cell-gather AABB, not a GPU far
 * plane. Four points use original low32/SAR15/signed IDIV/WORD arithmetic;
 * min/max retain all four, fixing PKR1098:30D1..33AB overwritten-slot loss. */
dok_render_scene_result dok_render_bounds(const dok_initial_world *world,
    const dok_nds_render_camera *camera, uint32_t heading,
    dok_render_gather_bounds *out);
/* Candidate bytes may change on error; output pointer is unchanged. The
 * immutable RU world and bindings must outlive the acquired frame. */
dok_render_scene_result dok_render_scene_prepare(dok_render_scene *scene,
    const dok_initial_world *world, const dok_geometry_binding bindings[256],
    const dok_nds_render_camera *camera, uint32_t heading,
    const dok_nds_render_frame **out);
dok_render_scene_result dok_render_scene_release(dok_render_scene *scene,
    uint32_t generation);
#endif
