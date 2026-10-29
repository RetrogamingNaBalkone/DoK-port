#ifndef DOK_NDS_EFFECTS_H
#define DOK_NDS_EFFECTS_H
#include "dok/render_scene.h"
#include "dok/balance.h"

/* Exact source scalar semantics, including WORD overflow and unsigned clamp. */
uint32_t dok_light_row(int32_t depth_q16,int16_t near_depth);
/* Source VGA6 is retained through percentage scaling, then quantized once.
 * LIGHT indexing precedes palette color conversion; no GE modulation. */
dok_status dok_light_palette(const unsigned char rgb6[768],
    const unsigned char light[4096],uint32_t percentage,uint16_t out[4096]);
/* N13 typed decoder of the original palette's RAW_SOURCE bytes in N04. The
 * reader must be the immutable reader used to open session. SHA, kind, size,
 * bounds and all VGA6 components are checked before publishing out. */
dok_status dok_effects_palette_load(const dok_asset_session *session,
    const dok_asset_reader *reader,dok_asset_id palette,unsigned char candidate[768],
    unsigned char out[768]);
/* Transform logical rows of a verified POT payload. All four native priority
 * pairs, division-before-multiply, cyclic/zero fill. index0 is retained for
 * opaque surfaces; masked rows zero every source index mapped to0 by LIGHT.
 * No source mutation or allocation. Source and output must not overlap. */
dok_status dok_effects_pixels(const unsigned char *source,uint16_t width,
    uint16_t height,uint16_t storage_width,uint16_t storage_height,
    uint16_t flags,uint16_t divisor,uint16_t phase,const unsigned char *light_row,
    uint32_t masked,unsigned char *out,uint32_t capacity);

typedef struct dok_water_state {
    int16_t counter, displayed_phase;
    uint32_t revision;
} dok_water_state;
/* One native water callback; counter18 cancels, getter rearm resets counter0
 * without changing the displayed phase. Interval is the native zero ticks.
 * Caller services N08 sweeps independently from presentation. */
dok_status dok_water_step(dok_water_state *state,uint32_t *cancel);
dok_status dok_water_rearm(dok_water_state *state,uint32_t *arm);

#define DOK_LIGHT_PIECES 48u
typedef struct dok_light_origin {
    int32_t x,y,z;
    uint32_t pieces;
} dok_light_origin;
typedef struct dok_light_workspace {
    dok_light_origin origins[DOK_RENDER_QUADS];
    dok_nds_render_quad pieces[DOK_LIGHT_PIECES];
    uint64_t edge_points[11264u];
} dok_light_workspace;
_Static_assert(sizeof(dok_light_workspace)<=DOK_NDS_SCRATCH_BYTES,"N13 scratch region");
typedef struct dok_light_counts {
    uint32_t source_quads,published_quads,split_quads,near_hidden;
    uint32_t rows[16];
} dok_light_counts;
/* DS adaptation of source screen-axis affine depth interpolation: vertical
 * faces split along projected X, horizontal faces along projected Y. Palette
 * row is packet.reserved0..15. Intersections use shared rational arithmetic,
 * rounded to the existing integer-world lattice (not a new render resolution).
 * Source domain admission precedes mutation. A later edge-weld capacity
 * refusal may change staging bytes; out remains unchanged. Caller discards
 * or releases that candidate and keeps the previously displayed GPU frame.
 * Original sample domain: camera/origins X/Z +/-32768, Y +/-4096.
 */
dok_render_scene_result dok_light_scene_apply(dok_render_scene *scene,
    int16_t near_depth,dok_light_workspace *workspace,dok_light_counts *out);
#endif
