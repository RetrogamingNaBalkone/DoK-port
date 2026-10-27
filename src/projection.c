#include "dok/projection.h"
#include <limits.h>

static int32_t signed_bits(uint32_t value)
{
    return value <= INT32_MAX ? (int32_t)value : -1 - (int32_t)~value;
}
static int32_t add(int32_t a,int32_t b)
{
    return signed_bits((uint32_t)a + (uint32_t)b);
}
static int32_t sub(int32_t a,int32_t b)
{
    return signed_bits((uint32_t)a - (uint32_t)b);
}
static int32_t mul(int32_t a,int32_t b)
{
    return signed_bits((uint32_t)a * (uint32_t)b);
}
static int32_t sar15(int32_t value)
{
    uint32_t bits=(uint32_t)value;
    return signed_bits((bits >> 15) | (value < 0 ? UINT32_C(0xfffe0000) : 0));
}

void dok_projection_transform(const dok_projection_camera *camera,
    int32_t x,int32_t y,int32_t z,dok_camera_point *out)
{
    int32_t dx=sub(x,camera->x), dz=sub(z,camera->z);
    out->lateral=sar15(add(mul(dx,camera->cosine),mul(dz,camera->sine)));
    out->depth=sar15(sub(mul(dz,camera->cosine),mul(dx,camera->sine)));
    out->vertical=sub(y,camera->y);
}

int dok_projection_project(const dok_projection_camera *camera,
    const dok_camera_point *point,dok_screen_point *out)
{
    int32_t numerator=mul(mul(camera->scale,camera->distance),camera->focal);
    int32_t denominator=add(point->depth,camera->distance);
    int32_t factor;
    if (!denominator || (numerator == INT32_MIN && denominator == -1)) return 0;
#ifdef DOK_PROJECTION_RECIP
    extern int32_t dok_gba_plane_divide(int32_t,unsigned);
    if((uint32_t)denominator-1<16384u)factor=dok_gba_plane_divide(numerator,(unsigned)denominator);
    else
#endif
    factor=numerator/denominator;
    out->x=add(sar15(mul(point->lateral,factor)),camera->center_x);
    out->y=add(sar15(mul(point->vertical,factor)),camera->center_y);
    out->factor=factor;
    return 1;
}
