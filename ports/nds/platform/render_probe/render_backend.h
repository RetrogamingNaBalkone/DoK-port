#ifndef DOK_NDS_RENDER_BACKEND_H
#define DOK_NDS_RENDER_BACKEND_H

#include <stddef.h>
#include <stdint.h>
#include "dok/render_packet.h"

#define DOK_NDS_RENDER_TEXTURE_CAPACITY 128u
#define DOK_NDS_RENDER_TEXTURE_BYTES (384u * 1024u)
#define DOK_NDS_RENDER_VERTEX_LIMIT 6144u
#define DOK_NDS_RENDER_POLYGON_LIMIT 2048u

typedef enum dok_nds_render_result {
    DOK_NDS_RENDER_OK = 0,
    DOK_NDS_RENDER_ARGUMENT = 1,
    DOK_NDS_RENDER_RANGE = 2,
    DOK_NDS_RENDER_CAPACITY = 3,
    DOK_NDS_RENDER_TEXTURE = 4,
    DOK_NDS_RENDER_TIMEOUT = 5,
    DOK_NDS_RENDER_HARDWARE = 6
} dok_nds_render_result;

/* Pixels are accepted N04 GPU_INDEX8_POT bytes. Palette has 256 RGB555
 * words, bit15 clear. Pointers remain owned and pinned by the caller. */
typedef struct dok_nds_render_texture {
    uint32_t asset_id;
    const uint8_t *pixels;
    uint32_t bytes;
    const uint16_t *palette;
    uint16_t storage_width, storage_height;
    uint16_t logical_width, logical_height;
    uint32_t color0_transparent;
} dok_nds_render_texture;

typedef struct dok_nds_render_stats {
    uint32_t input_quads, submitted_quads, submitted_vertices;
    uint32_t hardware_polygons, hardware_vertices;
    uint32_t disp3d_flags, gxstat_flags, rdlines;
    uint32_t texture_bytes, palette_bytes, uploads, upload_bytes;
    uint32_t geometry_wait_iterations, geometry_wait_ticks;
} dok_nds_render_stats;

typedef struct dok_nds_render_clip {
    int32_t x, y, z, w; /* Actual GE PosTest results, signed 20.12. */
} dok_nds_render_clip;

/* Startup/loading only. Allocates SDK texture metadata once; no per-frame
 * malloc. init sets VRAM mappings and the N06 off-axis projection. */
dok_nds_render_result dok_nds_render_init(void);

/* Complete level set. Performs full CPU admission before resetting old
 * textures. Publication happens in explicit loading mode with 3D disabled.
 * A later SDK upload failure leaves loading mode active, never a partial
 * textured scene. Calling code must account for this boundary explicitly. */
dok_nds_render_result dok_nds_render_preload(
    const dok_nds_render_texture *textures, uint32_t count,
    dok_nds_render_stats *stats);

/* Validates the whole frame before FIFO submission. Failure before submit
 * preserves the previously displayed scene. Frames use opaque quads and
 * no hardware face culling; the builder supplies proved visible surfaces.
 * stats reports counters before swap, then glFlush queues the next frame. */
dok_nds_render_result dok_nds_render_submit(
    const dok_nds_render_frame *frame, dok_nds_render_stats *stats);

/* Main calls after the queued frame has swapped at VBlank. It exposes the
 * 3D layer after a level preload; preload alone never reveals old geometry. */
void dok_nds_render_present(void);

/* Updates sticky diagnostic aggregate before any W1C control write.
 * Returns raw GFX_CONTROL, GXSTAT and previous-frame RDLINES sample. */
void dok_nds_render_observe(dok_nds_render_stats *stats);

/* Diagnostic only: loads the same cell modelview and uses actual PosTest.
 * It must not be interleaved with an in-flight submission. */
dok_nds_render_result dok_nds_render_position_test(
    const dok_nds_render_camera *camera, const dok_nds_render_cell *cell,
    const dok_nds_render_vertex *vertex, dok_nds_render_clip *out);

/* Read-only base N06 projection (column-major). Actual per-cell combined
 * projection is read by clip_matrix(), with identity position matrix. */
const int32_t *dok_nds_render_projection(void);

/* Diagnostic snapshot of the semantic modelview (not loaded into the GE)
 * and actual GE combined clip matrix after position_test. */
dok_nds_render_result dok_nds_render_cell_matrix(
    const dok_nds_render_camera *camera, const dok_nds_render_cell *cell,
    int32_t out[12]);
dok_nds_render_result dok_nds_render_clip_matrix(int32_t out[16]);

/* N12 demand residency. Plan allocates addresses without writing texels, then
 * submit queues geometry using those addresses. Commit copies pending payloads
 * only AFTER that geometry swaps at VBlank, before the GE raster starts.
 * CPU payloads remain pinned through commit. No old-frame/new-texel mixture.
 * reset is an explicit level loading boundary; its palette remains borrowed. */
typedef struct dok_nds_stream_texture {
    uint64_t key;
    dok_nds_render_texture texture;
} dok_nds_stream_texture;
typedef struct dok_nds_stream_stats {
    uint32_t hits, misses, evictions, rebuilds, uploads, upload_bytes;
    uint32_t resident_bytes, resident_count, commit_start_line, commit_end_line;
} dok_nds_stream_stats;
dok_nds_render_result dok_nds_stream_reset(const uint16_t *palette,uint32_t colors);
dok_nds_render_result dok_nds_stream_plan(const dok_nds_stream_texture *textures,
    uint32_t count,dok_nds_stream_stats *stats);
dok_nds_render_result dok_nds_stream_commit(dok_nds_stream_stats *stats);
int dok_nds_stream_loading(void);

/* Optional normal-frame pair: clean planned CPU sources before SwapBuffers,
 * then perform the same bounded VRAM commit after swap. Between these calls
 * the caller must preserve all source bytes, allocations, palette and pending
 * identities. No DMA, VRAM remapping or publication occurs in prepare.
 * Existing commit retains its complete source-cleaning contract. */
dok_nds_render_result dok_nds_stream_prepare_uploads(void);
dok_nds_render_result dok_nds_stream_commit_prepared(dok_nds_stream_stats *stats);
dok_nds_render_result dok_nds_stream_palette_dirty(void);
/* N13P explicit artistic adaptation: black depth fog, uncut geometry.
 * Configure between submissions. Source LIGHT remains the exact reference. */
void dok_nds_render_darkness(int enabled);

#endif
