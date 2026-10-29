#ifndef DOK_NDS_SPRITES_H
#define DOK_NDS_SPRITES_H
#include "dok/render_scene.h"
#include "dok/balance.h"
#include "dok/events.h"

#define DOK_SPRITE_LIMIT 512u
#define DOK_SPRITE_FRAME_LIMIT 1024u

typedef struct dok_sprite_frame {
    dok_asset_id image;
    int16_t interval, parameter_a, parameter_b;
    uint16_t width, height;
} dok_sprite_frame;
typedef struct dok_sprite_animation {
    const unsigned char *metadata;
    uint32_t bytes, count;
} dok_sprite_animation;
typedef struct dok_sprite_player {
    dok_sprite_animation animation;
    dok_sprite_frame displayed;
    int16_t cursor, remaining;
    uint32_t revision;
} dok_sprite_player;
/* Borrows verified N04 metadata. Native far pointers are replaced by asset IDs.
 * Explicit clamped accessor and sequential end are distinct operations. */
dok_status dok_sprite_animation_open(const dok_asset_view *view,
    dok_sprite_animation *out);
dok_status dok_sprite_animation_get(const dok_sprite_animation *animation,
    int32_t index, dok_sprite_frame *out);
/* One native base-animation callback: loops across END, decrements remaining,
 * publishes signed interval bits for N08 set_interval; no clock or render read.
 * remaining<=0 cancels. A successful step increments revision exactly once. */
dok_status dok_sprite_player_step(dok_sprite_player *player,
    uint32_t *interval, uint32_t *cancel);

typedef struct dok_sprite_pose {
    int32_t x, y, z;
    int16_t width, height;
    uint16_t flags, glyph;
    uint16_t texture_width, texture_height;
    dok_asset_id image;
    uint32_t source_id;
} dok_sprite_pose;
typedef struct dok_sprite_projection {
    int32_t lateral, depth, left, right, top, bottom, sort_key;
    uint32_t visible;
} dok_sprite_projection;
/* Original low32/SAR15 projection and SAR16(width,1/2), including negative odd
 * widths. This is an original-space observation, not the DS raster contract. */
dok_status dok_sprite_project(const dok_nds_render_camera *camera,
    const dok_sprite_pose *pose, dok_sprite_projection *out);

typedef struct dok_sprite_binding {
    dok_sprite_frame frame;
    uint16_t flags, reserved;
} dok_sprite_binding;
typedef struct dok_sprite_snapshot {
    dok_world_epoch world_epoch;
    uint32_t revision;
    const dok_sprite_binding *bindings; /* exactly 256 immutable glyph slots */
    const dok_initial_world *world; /* immutable occupancy and actor poses */
} dok_sprite_snapshot;
typedef struct dok_sprite_list {
    dok_sprite_pose poses[DOK_SPRITE_LIMIT];
    uint32_t count, considered, rejected, world_epoch, revision;
} dok_sprite_list;
/* Complete TMap3/6/9/10 + MMap3/9 in the accepted gather bounds. Initial actor
 * X/Z come from seeds, floor anchoring from the occupied FCMap cell. No AI.
 * Capacity errors are explicit; candidate bytes may change, out is unchanged. */
dok_status dok_sprites_gather(const dok_sprite_snapshot *snapshot,
    const dok_nds_render_camera *camera, uint32_t heading,
    dok_sprite_list *candidate, const dok_sprite_list **out);
/* N17 optional live flags and zero-visual-height bits, indexed by glyph.
 * Borrowed only for this immutable gather. NULL preserves exact N12 behavior. */
dok_status dok_sprites_gather_live(const dok_sprite_snapshot *,const uint16_t *runtime_flags,
    const unsigned char *zero_height_bits,const dok_nds_render_camera *,uint32_t heading,
    dok_sprite_list *candidate,const dok_sprite_list **out);
/* Append to an acquired door/base scene using caller-assigned texture slots.
 * All validation/admission precedes mutation. Opaque color0 holes are handled
 * by the texture, shared Z buffer handles walls, doors and other billboards.
 * Snapshot epoch/revision must match the gathered immutable list. */
dok_render_scene_result dok_sprites_append(dok_render_scene *scene,
    const dok_sprite_list *list, const uint16_t *texture_slots,
    uint32_t revision);
#endif
