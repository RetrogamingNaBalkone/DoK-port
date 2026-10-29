#include "dok/projection.h"

#include <limits.h>
#include <stdint.h>

_Static_assert(INT32_MAX == INT32_C(2147483647), "signed32 required");
_Static_assert(sizeof(dok_projection_camera) == 32u, "camera scalar layout");
_Static_assert(sizeof(dok_projection_vertex) == 12u, "vertex scalar layout");
_Static_assert(sizeof(dok_projection_sample) == 16u, "native sample scalar layout");
_Static_assert(sizeof(dok_projection_q16) == 8u, "mapped coordinate scalar layout");

int dok_n06_projection_compile_gate(void)
{
    dok_projection_result (*project)(const dok_projection_camera *,
                                     const dok_projection_vertex *,
                                     dok_projection_sample *) = dok_projection_native;
    dok_projection_result (*map)(dok_projection_band, int32_t, int32_t,
                                 dok_projection_q16 *) = dok_projection_map;
    return project != 0 && map != 0;
}
