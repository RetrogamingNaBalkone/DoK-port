/* Horizontal spans have no transparency branch in the native ASM. The
 * production port shares front-to-back coverage with vertical planes. */
#ifdef DOK_RENDER_EXACT_PLANES_PROTOTYPE
extern int32_t dok_gba_plane_divide(int32_t,unsigned);
#define PLANE_DIV(n,d) dok_gba_plane_divide((n),(unsigned)(d))
#elif defined(DOK_RENDER_FAST_PLANES_PROTOTYPE)
extern const uint32_t dok_plane_reciprocal[16385];
static int32_t plane_divide(int32_t value,unsigned divisor) {
    /* Q31 reciprocal: bounded sub-texel rounding change, no IDIV helper.
     * Projection bounds ensure positive divisors <=16384. */
    int64_t product=(int64_t)value*dok_plane_reciprocal[divisor];
    return (int32_t)((product+(value<0?INT64_C(2147483647):0))>>31);
}
#define PLANE_DIV(n,d) plane_divide((n),(unsigned)(d))
#else
#define PLANE_DIV(n,d) ((n)/(d))
#endif
#ifndef DOK_RENDER_WORLD_PLANES_PROTOTYPE
static int surface_covered(dok_renderer_workspace *w,unsigned x,unsigned y) {
#if defined(DOK_RENDERER_PROBE) && defined(DOK_RENDER_HALF_COLUMNS)
    (void)w;return (coverage_bits[x>>1][y>>5]>>(y&31))&1;
#else
    for(unsigned k=0;k<COVER_COUNT(w,x);++k) {
        if(COVER(w,x,k)[0]>y) return 0;
        if(COVER(w,x,k)[1]>=y) return 1;
    }
    return 0;
#endif
}
#endif
#if defined(DOK_RENDERER_PROBE) && defined(DOK_RENDER_HALF_COLUMNS)
/* Separate the common unshifted sampler from polygon setup. Apart from the
 * smaller register live set, the row mask and dimensions are invariant. */
typedef struct {
    const uint8_t *pixels,*lut;
    uint8_t *dst;
    unsigned width,height,x,last,y;
    int32_t tu,tv,du,dv;
    uint32_t *bits;
    unsigned mask;
#ifdef DOK_RENDER_HW_DIRECT_PROTOTYPE
    unsigned black;
#endif
} surface_span;
_Static_assert(offsetof(surface_span,bits)==48,"ARM surface sampler ABI");
extern unsigned dok_gba_surface_span(const surface_span *);
#ifdef DOK_RENDER_HW_DIRECT_PROTOTYPE
extern unsigned dok_gba_tiled_surface(const surface_span *);
#endif
#endif
#ifdef DOK_RENDER_HW_SURFACE_CLIP_PROTOTYPE
/* Trim already covered ends before preparing UV/light for a ceiling row.
 * The original unclipped left/right still determine the exact affine slope. */
static DOK_NOINLINE uint32_t surface_visible_range(unsigned first,unsigned last,unsigned y) {
    const uint32_t mask=1u<<(y&31);
    const uint32_t *bits=&coverage_bits[first>>1][y>>5];
    last&=~1u;
    while(first<=last && (*bits&mask)) {first+=2;bits+=4;}
    if(first>last)return UINT32_MAX;
    bits=&coverage_bits[last>>1][y>>5];
    while(last>first && (*bits&mask)) {last-=2;bits-=4;}
    return first|(last<<16);
}
#endif
#ifdef DOK_RENDER_HW_SURFACE_SPLIT_PROTOTYPE
#define SURFACE_RASTER_NAME surface_raster_hw
#define SURFACE_RASTER_ATTR DOK_NOINLINE
#define SURFACE_RASTER_PLANE 1
#define SURFACE_RASTER_TILED 1
#if defined(DOK_RENDER_HW_BG_LOWERED_PROTOTYPE) && !defined(DOK_RENDER_HW_LOW_ADAPTIVE_PROTOTYPE)
#include "renderer_surface_mask.inc"
#else
#include "renderer_surface_raster_body.inc"
#endif
#undef SURFACE_RASTER_NAME
#undef SURFACE_RASTER_ATTR
#undef SURFACE_RASTER_PLANE
#undef SURFACE_RASTER_TILED
#ifdef DOK_RENDER_HW_LOW_FEEDBACK_PROTOTYPE
#define SURFACE_RASTER_NAME surface_raster_mask
#define SURFACE_RASTER_ATTR DOK_COLD
#define SURFACE_RASTER_PLANE 1
#define SURFACE_RASTER_TILED 1
#include "renderer_surface_mask.inc"
#undef SURFACE_RASTER_NAME
#undef SURFACE_RASTER_ATTR
#undef SURFACE_RASTER_PLANE
#undef SURFACE_RASTER_TILED
#endif
#define SURFACE_RASTER_NAME surface_raster_general
#define SURFACE_RASTER_ATTR DOK_COLD
#else
#define SURFACE_RASTER_NAME surface_raster
#if defined(DOK_RENDER_COLUMN_PIPELINE_PROTOTYPE) || defined(DOK_RENDER_SECTOR_CLIP_PROTOTYPE)
#define SURFACE_RASTER_ATTR DOK_COLD
#else
#define SURFACE_RASTER_ATTR
#endif
#endif
#define SURFACE_RASTER_PLANE (f->plane)
#define SURFACE_RASTER_TILED (g_hw_tile_active)
#include "renderer_surface_raster_body.inc"
#undef SURFACE_RASTER_NAME
#undef SURFACE_RASTER_ATTR
#undef SURFACE_RASTER_PLANE
#undef SURFACE_RASTER_TILED
#ifdef DOK_RENDER_HW_SURFACE_SPLIT_PROTOTYPE
static void surface_raster(dok_renderer_workspace *w,const dok_runtime *r,const dok_renderer_surface *f,dok_renderer_stats *stats) {
#ifdef DOK_RENDER_HW_LOW_FEEDBACK_PROTOTYPE
    extern uint32_t g_hw_low_mode;
    extern unsigned g_hw_low_samples;
    if(g_hw_tile_active && f->plane==1) {
        unsigned before=stats->pixels_written;
        if(g_hw_low_mode)surface_raster_mask(w,r,f,stats);
        else surface_raster_hw(w,r,f,stats);
        g_hw_low_samples+=stats->pixels_written-before;
    }
#elif defined(DOK_RENDER_HW_LOW_ADAPTIVE_PROTOTYPE)
    extern uint32_t g_hw_low_mode;
    if(g_hw_tile_active && f->plane==1)surface_raster_hw(w,r,f,stats);
#else
    if(g_hw_tile_active && f->plane==1)surface_raster_hw(w,r,f,stats);
#endif
    else surface_raster_general(w,r,f,stats);
}
#endif
