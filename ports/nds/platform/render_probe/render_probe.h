#ifndef DOK_NDS_RENDER_PROBE_H
#define DOK_NDS_RENDER_PROBE_H

#include <stdint.h>

/* Observation only: all signed values use their uint32_t two's-complement
 * representation. CPU writes/flushes data before the final even sequence.
 * The external observer never writes emulated memory or hardware state. */
#define NDS_RENDER_PROBE_MAGIC 0x37524b44u
#define NDS_RENDER_HISTORY_MAGIC 0x37484b44u
#define NDS_RENDER_POSITION_MAGIC 0x37504b44u
#define NDS_RENDER_ABI_VERSION 1u
#define NDS_RENDER_HISTORY_CAPACITY 128u
#define NDS_RENDER_POSITION_CAPACITY 32u

enum {
    NDS_RENDER_BOOTING = 0u,
    NDS_RENDER_RUNNING = 1u,
    NDS_RENDER_COMPLETE = 2u,
    NDS_RENDER_FAILED = 3u,
    NDS_RENDER_READY = 4u
};
enum {
    NDS_RENDER_FRAME_LOADING = 1u,
    NDS_RENDER_FRAME_MOVING = 2u,
    NDS_RENDER_FRAME_WARMUP = 4u,
    NDS_RENDER_FRAME_PRESENTED = 8u,
    NDS_RENDER_FRAME_FLAT_DIAGNOSTIC = 16u,
    NDS_RENDER_FRAME_DIAGNOSTIC = 32u,
    NDS_RENDER_FRAME_SYNC = 64u
};

typedef struct NdsRenderProbe {
    uint32_t magic, abi_version, size_bytes, sequence;
    uint32_t status, error_bits, is_dsi;
    uint32_t case_count, case_id, level, frame_id, submitted_frame_id;
    uint32_t presented_frame_id, vblank_count, flags;
    uint32_t camera_x, camera_y, camera_z, sin_q15, cos_q15;
    uint32_t history_address, history_capacity, history_record_bytes;
    uint32_t history_total, position_address, position_count, position_record_bytes;
    uint32_t projection_address, projection_words;
    uint32_t timer_hz, timer_divider, vblank_ticks;
    uint32_t total_frames, max_vblank_delta, max_frame_ticks;
    uint32_t disp3d_sticky, gxstat_sticky, rdlines_min, rdlines_max, rdlines_samples;
    uint32_t texture_capacity_bytes, palette_capacity_bytes;
    uint32_t texture_bytes, palette_bytes, texture_count;
    uint32_t uploads_total, upload_bytes_total, cache_misses, cache_evictions;
    uint32_t vram_a, vram_b, vram_c, vram_d, vram_e;
    uint32_t vram_f, vram_g, vram_h, vram_i;
    uint32_t region_base, region_limit, static_end, heap_start, heap_end;
    uint32_t heap_limit_before, heap_limit_policy, heap_limit_final;
    uint32_t reserve_bytes, managed_base, managed_bytes, malloc_request_bytes;
    uint32_t static_startup_bytes, sdk_heap_available, sdk_heap_min;
    uint32_t world_used, world_high_water, scratch_used, scratch_high_water;
    uint32_t scene_used, scene_high_water, assets_used, assets_high_water;
    uint32_t source_packet_address, source_packet_bytes;
    uint32_t render_frame_address, render_frame_bytes;
    uint32_t cell_record_bytes, quad_record_bytes, render_generation, flat_diagnostic;
    uint32_t negative_pass_mask;
} NdsRenderProbe;

/* Each slot has its own seqlock because a ring overwrite may overlap an
 * external read. ordinal starts at 1; slot=(ordinal-1)%capacity. Loading and
 * warmup records remain present, so their stalls cannot disappear from data.
 * GPU geometry-wait ticks are not full rasterizer execution time. */
typedef struct NdsRenderHistoryRecord {
    uint32_t magic, abi_version, size_bytes, sequence;
    uint32_t ordinal, case_id, level, frame_id, flags;
    uint32_t camera_x, camera_y, camera_z, sin_q15, cos_q15;
    uint32_t vblank_start, vblank_end, presented_frame_id;
    uint32_t prepare_ticks, io_upload_ticks, submit_ticks, geometry_wait_ticks;
    uint32_t total_ticks, input_quads, submitted_quads, submitted_vertices;
    uint32_t hardware_polygons, hardware_vertices;
    uint32_t disp3d_flags, gxstat_flags, rdlines;
    uint32_t texture_bytes, palette_bytes, uploads, upload_bytes;
} NdsRenderHistoryRecord;

/* Actual GE PosTest under the same cell clip transform as rendering.
 * local vertices are integer world units, GPU v16 input=local*8. Results are
 * signed 20.12. modelview is the semantic camera/cell 4x3 transform; the actual
 * GE pipeline loads an origin-linear clip matrix and identity modelview.
 * clipmatrix is read from GE. This is not a bitmap-derived expectation. */
typedef struct NdsRenderPositionRecord {
    uint32_t magic, abi_version, size_bytes, sequence;
    uint32_t ordinal, case_id;
    uint32_t origin_x, origin_y, origin_z;
    uint32_t local_x, local_y, local_z;
    uint32_t camera_x, camera_y, camera_z, sin_q15, cos_q15;
    uint32_t clip_x, clip_y, clip_z, clip_w;
    uint32_t modelview_0, modelview_1, modelview_2, modelview_3;
    uint32_t modelview_4, modelview_5, modelview_6, modelview_7;
    uint32_t modelview_8, modelview_9, modelview_10, modelview_11;
    uint32_t clipmatrix_0, clipmatrix_1, clipmatrix_2, clipmatrix_3;
    uint32_t clipmatrix_4, clipmatrix_5, clipmatrix_6, clipmatrix_7;
    uint32_t clipmatrix_8, clipmatrix_9, clipmatrix_10, clipmatrix_11;
    uint32_t clipmatrix_12, clipmatrix_13, clipmatrix_14, clipmatrix_15;
} NdsRenderPositionRecord;

extern volatile NdsRenderProbe g_nds_render_probe;
extern volatile NdsRenderHistoryRecord g_nds_render_history[NDS_RENDER_HISTORY_CAPACITY];
extern volatile NdsRenderPositionRecord g_nds_render_positions[NDS_RENDER_POSITION_CAPACITY];

#endif
