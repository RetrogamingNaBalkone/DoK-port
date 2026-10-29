#include "dok/projection.h"

#include <limits.h>
#include <stddef.h>

static int32_t wrap32(int64_t value)
{
    uint32_t low = (uint32_t)(uint64_t)value;
    return low <= (uint32_t)INT32_MAX ? (int32_t)low :
           (int32_t)((int64_t)low - INT64_C(4294967296));
}

static int32_t sar15(int32_t value)
{
    int64_t wide = value;
    return wide >= 0 ? (int32_t)(wide / INT64_C(32768)) :
           (int32_t)(-((-wide + INT64_C(32767)) / INT64_C(32768)));
}

dok_projection_result dok_projection_native(const dok_projection_camera *camera,
                                          const dok_projection_vertex *vertex,
                                          dok_projection_sample *out)
{
    dok_projection_sample result;
    int32_t dx, dz, lateral, numerator, denominator;
    if (camera == NULL || vertex == NULL || out == NULL) {
        return DOK_PROJECTION_INVALID_ARGUMENT;
    }
    dx = wrap32((int64_t)vertex->x - camera->x);
    dz = wrap32((int64_t)vertex->z - camera->z);
    lateral = sar15(wrap32((int64_t)wrap32((int64_t)dx * camera->cos_q15) +
                          wrap32((int64_t)dz * camera->sin_q15)));
    result.depth = sar15(wrap32((int64_t)wrap32((int64_t)dz * camera->cos_q15) -
                               wrap32((int64_t)dx * camera->sin_q15)));
    numerator = wrap32((int64_t)wrap32((int64_t)camera->viewport_scale *
                                      camera->distance) * camera->focal_q12);
    denominator = wrap32((int64_t)result.depth + camera->distance);
    if (denominator == 0 || (numerator == INT32_MIN && denominator == -1)) {
        return DOK_PROJECTION_DIVIDE_FAULT;
    }
    result.factor_q15 = numerator / denominator;
    result.x = wrap32((int64_t)camera->center_x +
                     sar15(wrap32((int64_t)lateral * result.factor_q15)));
    result.y = wrap32((int64_t)camera->center_y +
                     sar15(wrap32((int64_t)wrap32((int64_t)vertex->y - camera->y) *
                                  result.factor_q15)));
    *out = result;
    return DOK_PROJECTION_OK;
}

static int q16_nearest(int64_t numerator, int64_t denominator, int32_t *out)
{
    int64_t scaled = numerator * INT64_C(65536);
    int64_t magnitude = scaled < 0 ? -scaled : scaled;
    int64_t rounded = (magnitude + denominator / 2) / denominator;
    if (scaled < 0) {
        rounded = -rounded;
    }
    if (rounded < INT32_MIN || rounded > INT32_MAX) {
        return 0;
    }
    *out = (int32_t)rounded;
    return 1;
}

dok_projection_result dok_projection_map(dok_projection_band band,
                                       int32_t source_x, int32_t source_y,
                                       dok_projection_q16 *out)
{
    dok_projection_q16 result;
    int64_t numerator_y, denominator_y;
    if (out == NULL) {
        return DOK_PROJECTION_INVALID_ARGUMENT;
    }
    switch (band) {
    case DOK_PROJECTION_SCENE:
        numerator_y = INT64_C(154) * source_y - 3;
        denominator_y = 160;
        break;
    case DOK_PROJECTION_HUD:
        numerator_y = INT64_C(38) * source_y + 79;
        denominator_y = 40;
        break;
    case DOK_PROJECTION_STATIC:
        numerator_y = INT64_C(48) * source_y - 1;
        denominator_y = 50;
        break;
    default:
        return DOK_PROJECTION_INVALID_ARGUMENT;
    }
    /* Largest intermediate is < 2^55 for every signed32 input. */
    if (!q16_nearest(INT64_C(8) * source_x - 1, 10, &result.x) ||
        !q16_nearest(numerator_y, denominator_y, &result.y)) {
        return DOK_PROJECTION_RANGE;
    }
    *out = result;
    return DOK_PROJECTION_OK;
}
