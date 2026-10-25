#ifndef DOK_PROJECTION_H
#define DOK_PROJECTION_H
#include <stdint.h>

/* G04 scalar reconstruction, not visibility/rasterization. See rendering.md.
 * Original x86 low32 IMUL/ADD/SUB and SAR15 are deliberate, including wrap.
 * Focal is a runtime value: do not assume RU/EN data offsets are identical. */
typedef struct {
    int32_t x, y, z, sine, cosine, distance;
    int16_t scale, focal, center_x, center_y;
} dok_projection_camera;
typedef struct { int32_t lateral, depth, vertical; } dok_camera_point;
typedef struct { int32_t x, y, factor; } dok_screen_point;

void dok_projection_transform(const dok_projection_camera *camera,
    int32_t x, int32_t y, int32_t z, dok_camera_point *out);
/* Pre-vertex-narrowing coordinates. No frustum/near clipping performed.
 * Returns0 on invalid signed IDIV (zero divisor/INT_MIN divided by-1);
 * on failure out is untouched. Caller must validate non-null pointers. */
int dok_projection_project(const dok_projection_camera *camera,
    const dok_camera_point *point, dok_screen_point *out);
#endif
