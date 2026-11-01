/* Horizontal spans have no transparency branch in the native ASM. The
 * production port shares front-to-back coverage with vertical planes. */
#ifdef DOK_RENDER_EXACT_PLANES_PROTOTYPE
extern int32_t dok_gba_plane_divide(int32_t,unsigned);
#ifdef DOK_RENDER_HW_PLANE_PAIR_PROTOTYPE
extern uint64_t dok_gba_plane_divide_pair(int32_t,int32_t,unsigned);
#endif
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
#ifdef DOK_RENDER_HW_PLANE_ACTIVE_PROTOTYPE
typedef struct {int first,last;int32_t x,u,v,dx,du,dv;} surface_edge;
#ifdef DOK_RENDER_HW_PLANE_ACTIVE_THUMB_PROTOTYPE
static DOK_NOINLINE __attribute__((target("thumb")))
#elif defined(DOK_RENDER_HW_PLANE_ACTIVE_HOT_PROTOTYPE)
static DOK_NOINLINE
#else
static DOK_COLD
#endif
uint32_t surface_active_edges(const surface_edge *edges,int y,int offset) {
    int lo=-1,hi=-1,count=0,until=143;int32_t xl=0,xh=0;
    for(int k=0;k<4;++k) if(edges[k].first!=edges[k].last) {
        if(y<edges[k].first) {
            if(edges[k].first-1<until)until=edges[k].first-1;
        } else if(y<=edges[k].last) {
            int32_t x=add(edges[k].x,mul(offset,edges[k].dx));
            if(lo<0 || x<xl) {lo=k;xl=x;}
            if(hi<0 || x>xh) {hi=k;xh=x;}
            ++count;if(edges[k].last<until)until=edges[k].last;
        }
    }
    /* At a vertex row several edges may meet. Re-evaluate the following row
     * rather than carrying one of the coincident endpoint slopes forward. */
    if(count>2 || lo==hi)until=y;
    return (unsigned)(lo+1)|((unsigned)(hi+1)<<4)|((unsigned)count<<8)|((unsigned)until<<16);
}
#endif
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
#ifndef DOK_RENDER_HW_KERNEL_PROFILE_PROTOTYPE
extern unsigned dok_gba_tiled_surface(const surface_span *);
#endif
#endif
#endif
#ifdef DOK_RENDER_HW_SPAN_CACHE_PROTOTYPE
/* G24 bounded experiment: four immutable prelit sampled rows. No coverage or
 * geometry is cached. Every hit composes at the original front-to-back point.
 * Key contains all sample-affecting state; RAM textures always bypass. */
typedef struct {uint32_t key[10];uint8_t colors[120];} hw_span_cache_entry;
static hw_span_cache_entry hw_span_cache[4];
volatile uint32_t g_hw_span_cache_work[4]; /* eligible,hits,misses,hit writes */
_Static_assert(sizeof(hw_span_cache)+sizeof(g_hw_span_cache_work)==656,"G24 EWRAM budget");
static DOK_NOINLINE unsigned hw_cached_surface(const surface_span *s) {
#ifdef DOK_RENDER_HW_SPAN_CACHE_TOP_PROTOTYPE
    /* Focus the same four slots on ceiling rows; only the foremost left-edge
     * span may populate them, preventing competing surfaces from thrashing. */
    if(s->y<24 || s->y>=28 || s->x || (*s->bits&s->mask) || (uintptr_t)s->lut<0x08000000u || (uintptr_t)s->lut>=0x0a000000u)
        return dok_gba_tiled_surface(s);
    hw_span_cache_entry *e=&hw_span_cache[s->y-24];
#else
    if((s->y&31u) || s->y>=128 || (uintptr_t)s->lut<0x08000000u || (uintptr_t)s->lut>=0x0a000000u)
        return dok_gba_tiled_surface(s);
    hw_span_cache_entry *e=&hw_span_cache[s->y>>5];
#endif
    uint32_t key[10]={(uint32_t)(uintptr_t)s->lut,s->width,s->height,s->x,s->last,
        (uint32_t)s->tu,(uint32_t)s->tv,(uint32_t)s->du,(uint32_t)s->dv,s->black};
    unsigned hit=1,count=(s->last-s->x)/2+1;
    ++g_hw_span_cache_work[0];
    for(unsigned k=0;k<10;++k)if(e->key[k]!=key[k]){hit=0;break;}
    if(hit)++g_hw_span_cache_work[1];
    else {
        ++g_hw_span_cache_work[2];
        uint32_t u=(uint32_t)s->tu,v=(uint32_t)s->tv;
        for(unsigned k=0;k<count;++k) {
            int x=(int32_t)u>>16,y=(int32_t)v>>16;
            if(x<0)x=0;else if((unsigned)x>=s->width)x=(int)s->width-1;
            if(y<0)y=0;else if((unsigned)y>=s->height)y=(int)s->height-1;
            unsigned c=s->lut[(unsigned)y*s->width+(unsigned)x];
            e->colors[k]=(uint8_t)(c?c:s->black);
            u+=(uint32_t)s->du*2u;v+=(uint32_t)s->dv*2u;
        }
        for(unsigned k=0;k<10;++k)e->key[k]=key[k];
    }
    unsigned written=0;uint8_t *dst=s->dst;uint32_t *bits=s->bits;
    for(unsigned k=0;k<count;++k) {
        if(!(*bits&s->mask)){*bits|=s->mask;*dst=e->colors[k];++written;}
        ++dst;if(((uintptr_t)dst&7u)==4u)dst+=56;
        bits+=4;
    }
    if(hit)g_hw_span_cache_work[3]+=written;
    return written;
}
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
