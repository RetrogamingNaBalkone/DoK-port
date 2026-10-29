#ifndef DOK_NDS_DOOR_RENDER_H
#define DOK_NDS_DOOR_RENDER_H
#include "dok/interactions.h"
#include "dok/render_scene.h"

typedef enum dok_door_geometry_result {
    DOK_DOOR_GEOMETRY_KEEP=0, DOK_DOOR_GEOMETRY_HIDDEN=1,
    DOK_DOOR_GEOMETRY_ARGUMENT=2, DOK_DOOR_GEOMETRY_UNSUPPORTED=3
} dok_door_geometry_result;
typedef struct dok_door_render_counts {
    uint32_t world_epoch, revision;
    uint32_t doors, cropped, hidden;
} dok_door_render_counts;

/* Pure presentation of one original N07 central plane. Constructor mirror is
 * already encoded in source UV. Only native right/down zero-fill crops are
 * supported. Integer XYZ intersections round to nearest world unit; texture
 * addressing remains Q16. phase0 is byte-identical. At extent>1 and terminal
 * phase the panel is absent: an explicit NDS correction of the native strip.
 * Ready-open Use may resample extent below the retained phase; phase>=extent
 * is a legal hidden panel, not an invalid simulation state.
 * No SDK, allocation, clock, collision or world mutation. out is unchanged on
 * HIDDEN or failure, and may alias source. */
dok_door_geometry_result dok_door_geometry_build(const dok_geometry_quad *source,
    const dok_geometry_image *image, int16_t phase, int16_t extent,
    dok_geometry_quad *out);

/* Additive adapter over unchanged N07 prepare. It crops/compacts caller-owned
 * staging before publishing the lease, with no second mesh. NULL phases uses
 * the exact N07 path. A copied phase view and world/bindings must describe the
 * same immutable simulation boundary and remain unchanged until release.
 * Caller, not this pure renderer, enforces the interaction/collision borrow.
 * Source IDs survive compaction. A changed door participating in an N07 merge
 * is rejected rather than silently cropped as one shared phase. On failure
 * out/counts stay unchanged and any acquired base lease is released. */
dok_render_scene_result dok_door_scene_prepare(dok_render_scene *scene,
    const dok_initial_world *world, const dok_geometry_binding bindings[256],
    const dok_nds_render_camera *camera, uint32_t heading,
    const dok_interaction_phase_view *phases, dok_door_render_counts *counts,
    const dok_nds_render_frame **out);
#endif
