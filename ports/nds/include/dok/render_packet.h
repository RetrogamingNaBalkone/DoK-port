#ifndef DOK_RENDER_PACKET_H
#define DOK_RENDER_PACKET_H

#include <stdint.h>

typedef struct dok_nds_render_camera {
    int32_t x, y, z;
    int32_t sin_q15, cos_q15;
} dok_nds_render_camera;

typedef struct dok_nds_render_vertex {
    int32_t x, y, z; /* Local integer world units; one GPU unit = 512. */
    int32_t u_q16, v_q16; /* Logical texel coordinates, before POT padding. */
} dok_nds_render_vertex;

typedef struct dok_nds_render_quad {
    dok_nds_render_vertex vertex[4];
    uint16_t texture_slot;
    uint16_t flags; /* Reserved, must be zero for N07 opaque surfaces. */
    uint32_t source_surface_id;
    uint16_t color_rgb555; /* Used only by diagnostic texture_slot=UINT16_MAX. */
    uint16_t reserved;
} dok_nds_render_quad;

typedef struct dok_nds_render_cell {
    int32_t origin_x, origin_y, origin_z;
    const dok_nds_render_quad *quads;
    uint32_t quad_count;
} dok_nds_render_cell;

typedef struct dok_nds_render_frame {
    dok_nds_render_camera camera;
    const dok_nds_render_cell *cells;
    uint32_t cell_count;
    uint32_t generation;
} dok_nds_render_frame;

#endif
