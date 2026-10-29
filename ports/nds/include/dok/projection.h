#ifndef DOK_PROJECTION_H
#define DOK_PROJECTION_H

#include <stdint.h>

/* N06 DOS numerical oracle; not a GPU vertex representation or clipper. */
typedef struct dok_projection_camera {
    int32_t x, y, z;
    int32_t sin_q15, cos_q15;
    int32_t distance;
    int16_t viewport_scale, focal_q12;
    int16_t center_x, center_y;
} dok_projection_camera;

typedef struct dok_projection_vertex {
    int32_t x, y, z;
} dok_projection_vertex;

typedef struct dok_projection_sample {
    int32_t x, y, depth, factor_q15;
} dok_projection_sample;

typedef struct dok_projection_q16 {
    int32_t x, y;
} dok_projection_q16;

typedef enum dok_projection_result {
    DOK_PROJECTION_OK = 0,
    DOK_PROJECTION_INVALID_ARGUMENT = 1,
    DOK_PROJECTION_DIVIDE_FAULT = 2,
    DOK_PROJECTION_RANGE = 3
} dok_projection_result;

typedef enum dok_projection_band {
    DOK_PROJECTION_SCENE = 0,
    DOK_PROJECTION_HUD = 1,
    DOK_PROJECTION_STATIC = 2
} dok_projection_band;

/* Preserves original low32 IMUL/ADD/SUB, signed SAR15 and IDIV semantics.
 * Does not test visibility, clip polygons, narrow raster vertices or allocate.
 * Invalid division reports DIVIDE_FAULT; every failure leaves *out unchanged.
 */
dok_projection_result dok_projection_native(const dok_projection_camera *camera,
                                          const dok_projection_vertex *vertex,
                                          dok_projection_sample *out);

/* Map global source pixel-index coordinates; HUD source y is 160..199.
 * Offscreen coordinates are supported within signed Q16 output range.
 * Exact half-pixel rational map rounded to nearest Q16, ties away from zero.
 * No implicit clipping. Every failure leaves *out unchanged.
 */
dok_projection_result dok_projection_map(dok_projection_band band,
                                       int32_t source_x, int32_t source_y,
                                       dok_projection_q16 *out);

#endif
