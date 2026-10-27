#include "dok/renderer.h"
#include "dok/image.h"
#include "dok/animation.h"
#include <limits.h>
#include "renderer_trig.inc"
#if defined(DOK_RENDER_HW_DIRECT_RUN_PROTOTYPE) && !defined(DOK_RENDER_NO_SCALES_PROTOTYPE)
#error HW_DIRECT_RUN requires disabled precomputed strips
#endif
#if defined(_MSC_VER)
#define DOK_NOINLINE __declspec(noinline)
#else
#define DOK_NOINLINE __attribute__((noinline))
#endif
#ifdef DOK_RENDER_TRACE
#include <stdio.h>
#define PROFILE_FAIL() do {fprintf(stderr,"renderer profile line %d\n",__LINE__);return DOK_RENDER_PROFILE;} while(0)
#else
#define PROFILE_FAIL() return DOK_RENDER_PROFILE
#endif

#ifdef DOK_RENDERER_PROBE
#define DOK_COLD __attribute__((noinline,section(".rom.text")))
extern void gba_render_stage(unsigned stage);
#define STAGE(n) gba_render_stage(n)
/* Optional per-primitive attribution. Zero by default, so normal frame
 * measurements include only its branch, never the timestamp calls. */
volatile uint32_t g_render_work[5];
#ifdef DOK_RENDER_HW_FACE_CLASS_PROFILE_PROTOTYPE
volatile uint32_t g_hw_face_class_work[4]; /* masked/opaque ticks, then calls */
#endif
#ifdef DOK_RENDER_HW_GEOMETRY_PROFILE_PROTOTYPE
volatile uint32_t g_geometry_work[8]; /* sides,surfaces,sprites,image: ticks/calls */
#endif
#ifdef DOK_RENDER_RASTER_PROFILE_PROTOTYPE
volatile uint32_t g_raster_work[4]; /* visible ticks/calls,mapped ticks/calls */
#endif
static DOK_COLD uint32_t render_ticks(void) {
    volatile const uint16_t *clock=(volatile const uint16_t *)0x04000108;
    uint16_t hi,lo,next;
    do {hi=clock[2];lo=clock[0];next=clock[2];} while(hi!=next);
    return ((uint32_t)hi<<16)|lo;
}
/* The single GBA renderer owns these during a draw. IRQs never render.
 * Keep the overwhelmingly common first interval on the fast 32-bit bus. */
#ifdef DOK_RENDER_HALF_COLUMNS
#define COVER_X(x) ((x)>>1)
#define COVER_WIDTH 120
#else
#define COVER_X(x) (x)
#define COVER_WIDTH 240
#endif
static uint8_t coverage_count[COVER_WIDTH] __attribute__((section(".iwram.data")))={0};
#ifdef DOK_RENDER_HW_INTERVAL_EWRAM_PROTOTYPE
static uint8_t coverage_first[COVER_WIDTH][2]={{0}};
#else
static uint8_t coverage_first[COVER_WIDTH][2] __attribute__((section(".iwram.data")))={{0}};
#endif
static uint16_t face_order[DOK_RENDER_FACE_CAPACITY];
#ifdef DOK_RENDER_HALF_COLUMNS
/* Horizontal surfaces publish coverage a pixel at a time. Keep their bitset
 * on the fast bus and reconstruct intervals only when a wall needs them. */
#ifdef DOK_RENDER_HW_COVER4_PROTOTYPE
static uint32_t coverage_bits[120][4] __attribute__((section(".iwram.data")))={{0}};
#else
static uint32_t coverage_bits[120][5] __attribute__((section(".iwram.data")))={{0}};
#endif
static void coverage_mark(unsigned x,unsigned lo,unsigned hi) {
    uint32_t *bits=coverage_bits[x>>1];
    unsigned first=lo>>5,last=hi>>5;
    uint32_t a=UINT32_MAX<<(lo&31),b=UINT32_MAX>>(31-(hi&31));
    if(first==last) bits[first]|=a&b;
    else {bits[first]|=a;for(unsigned k=first+1;k<last;++k) bits[k]=UINT32_MAX;bits[last]|=b;}
}
#ifdef DOK_RENDER_HW_CONTAINS_CALL_PROTOTYPE
static DOK_NOINLINE int coverage_contains(unsigned x,unsigned lo,unsigned hi) {
#else
static int coverage_contains(unsigned x,unsigned lo,unsigned hi) {
#endif
    const uint32_t *bits=coverage_bits[x>>1];
    unsigned first=lo>>5,last=hi>>5;
    uint32_t a=UINT32_MAX<<(lo&31),b=UINT32_MAX>>(31-(hi&31));
    if(first==last) return (bits[first]&(a&b))==(a&b);
    if((bits[first]&a)!=a || (bits[last]&b)!=b) return 0;
    for(unsigned k=first+1;k<last;++k) if(bits[k]!=UINT32_MAX) return 0;
    return 1;
}
#define COVER_MARK(x,lo,hi) coverage_mark(x,lo,hi)
#else
#define COVER_MARK(x,lo,hi) ((void)0)
#endif
#ifdef DOK_RENDER_STATIC_SETUP_PROTOTYPE
/* Single GBA workspace: descriptors survive draws, while the public known
 * flags are still reset and published at the original consideration points. */
extern const int16_t dok_static_insets[6][256];
extern const uint8_t dok_static_cells[6][3][4096];
#ifdef DOK_RENDER_HW_CELL_CLASS_PROTOTYPE
extern const uint8_t dok_sprite_class_masks[6][256];
#endif
static unsigned static_view;
static uint8_t image_cache_view[256],surface_cache_view[256][3];
#ifdef DOK_RENDER_HW_ANIM_CACHE_PROTOTYPE
static uint32_t image_animation_key[256];
#endif
static uint8_t lights_cache_view;
#endif
#define COVER_COUNT(w,x) coverage_count[COVER_X(x)]
#define COVER(w,x,k) ((k)?(w)->covered[x][k]:coverage_first[COVER_X(x)])
#else
#define DOK_COLD
#define STAGE(n) ((void)0)
#define COVER_COUNT(w,x) ((w)->covered_count[x])
#define COVER(w,x,k) ((w)->covered[x][k])
#define COVER_MARK(x,lo,hi) ((void)0)
#endif
#if defined(DOK_RENDERER_PROBE) && defined(DOK_RENDER_HALF_COLUMNS)
#ifdef DOK_RENDER_HW_COVER_ROM_PROTOTYPE
static DOK_COLD unsigned coverage_edge(const uint32_t *bits,unsigned y,uint32_t invert) {
#else
static DOK_NOINLINE unsigned coverage_edge(const uint32_t *bits,unsigned y,uint32_t invert) {
#endif
    while(y<144) {
        uint32_t v=(bits[y>>5]^invert)>>(y&31);
        if(v) {
            unsigned n=0;
            if(!(v&65535)) {v>>=16;n+=16;}
            if(!(v&255)) {v>>=8;n+=8;}
            if(!(v&15)) {v>>=4;n+=4;}
            if(!(v&3)) {v>>=2;n+=2;}
            if(!(v&1)) ++n;
            return y+n<144?y+n:144;
        }
        y=(y|31)+1;
    }
    return 144;
}
#endif
#if defined(DOK_RENDERER_PROBE) && defined(DOK_RENDER_HALF_COLUMNS)
static void coverage_sync(dok_renderer_workspace *w,unsigned x) {
    if(!(COVER_COUNT(w,x)&128)) return;
    unsigned count=0,y=0;
    const uint32_t *bits=coverage_bits[x>>1];
    while((y=coverage_edge(bits,y,0))<144) {
        unsigned lo=y;
        y=coverage_edge(bits,y,UINT32_MAX);
        COVER(w,x,count)[0]=(uint8_t)lo;COVER(w,x,count)[1]=(uint8_t)(y-1);++count;
    }
    COVER_COUNT(w,x)=(uint8_t)count;
}
#else
#define coverage_sync(w,x) ((void)0)
#endif
static void zero(void *p,size_t n) {uint8_t *b=p;while(n--) *b++=0;}
static int32_t bits(uint32_t v) {return v<=INT32_MAX?(int32_t)v:-1-(int32_t)~v;}
static int16_t word(int32_t v) {uint16_t n=(uint16_t)v;return n<=32767?(int16_t)n:(int16_t)(-1-(int16_t)(65535u-n));}
static int32_t sar(int32_t v,unsigned n) {uint32_t b=(uint32_t)v;return bits((b>>n)|(v<0?(UINT32_MAX<<(32-n)):0));}
static int32_t mul(int32_t a,int32_t b) {return bits((uint32_t)a*(uint32_t)b);}
static int32_t add(int32_t a,int32_t b) {return bits((uint32_t)a+(uint32_t)b);}
static int32_t sub(int32_t a,int32_t b) {return bits((uint32_t)a-(uint32_t)b);}
#ifdef DOK_RENDER_STREAM_BSP_PROTOTYPE
#include "renderer_stream_state.inc"
#define RENDER_DEFER_STATIC stream_active
#endif
#ifdef DOK_RENDER_COLUMN_PIPELINE_PROTOTYPE
#include "renderer_columns_state.inc"
#define RENDER_DEFER_STATIC column_active
#endif
#ifdef DOK_RENDER_SECTOR_CLIP_PROTOTYPE
#include "renderer_sector_state.inc"
#define RENDER_DEFER_STATIC sector_active
#endif
static const dok_renderer_image *face_image(const dok_renderer_workspace *w,unsigned id) {
 return id<256?&w->images[id]:&w->instance_images[id-256];
}
#ifdef DOK_RENDER_PRELIT_PROTOTYPE
static const uint32_t *prelit_view;
#ifdef DOK_RENDER_HW_SPARSE_POSTS_PROTOTYPE
static const uint32_t *sparse_view;
static const uint16_t *hw_column_posts;
extern const uint32_t dok_sparse_image_offsets[];
extern const uint8_t dok_sparse_posts[];
#define SET_COLUMN_POSTS(im,prelit,u) do { \
    if((im)->flags&8u) { \
        unsigned table=sparse_view[(im)->resource_id*16+row]; \
        hw_column_posts=(const uint16_t *)(const void *)(dok_sparse_posts+ \
            ((const uint32_t *)(const void *)(dok_sparse_posts+table))[u]); \
    } \
} while(0)
#endif
#endif
#ifdef DOK_RENDER_HW_SURFACE_PRELIT_PROTOTYPE
extern const uint8_t dok_ceiling_bank[];
extern const uint32_t dok_ceiling_offsets[6][16];
#endif
static int32_t scale_x(int32_t x) {return x>=0?x*3/4:-(((-x)*3+3)/4);}
static int32_t scale_y(int32_t y) {return y>=0?y*9/10:-(((-y)*9+9)/10);}
static int clamp(int x,int a,int b) {return x<a?a:(x>b?b:x);}
#ifdef DOK_RENDER_HW_NONZERO_COLUMN_PROTOTYPE
static unsigned hw_column_nonzero;
#ifdef DOK_RENDER_HW_SPAN_SOLID_PROTOTYPE
static uint32_t hw_column_zero_bits;
static DOK_NOINLINE int span_has_no_zero(uint32_t zero_bits,unsigned v,unsigned step,unsigned count) {
    if(!count)return 0;
    unsigned first=v>>20,last=(v+(count-1)*step)>>20;
    if(last>=32 || first>last)return 0;
    uint32_t mask=(UINT32_MAX<<first)&(UINT32_MAX>>(31-last));
    return !(zero_bits&mask);
}
#endif
#define COLUMN_MODE(flags,solid) ((solid)?0x80000000u:((flags)&8u)?1u:0u)
#else
#define COLUMN_MODE(flags,solid) (((flags)&8u) && !(solid))
#endif
#ifdef DOK_RENDER_HW_ALPHA_EMPTY_PROTOTYPE
#define PRELIT_COLUMN_MODE(flags,offset,u) (COLUMN_MODE(flags,dok_prelit_bank[(offset)-256+(u)]) | \
    (((flags)&8u)?((unsigned)dok_prelit_bank[(offset)-512+(u)]<<29):0u))
#else
#define PRELIT_COLUMN_MODE(flags,offset,u) COLUMN_MODE(flags,dok_prelit_bank[(offset)-256+(u)])
#endif
#ifdef DOK_RENDER_HW_DIRECT_PROTOTYPE
uint32_t g_hw_tile_active,g_hw_tile_black;
#ifdef DOK_RENDER_HW_BG_PROTOTYPE
int g_hw_bg_envelope[4];
volatile uint32_t g_hw_plane_work[6]; /* Timed only when g_render_work[0] is set. */
#ifdef DOK_RENDER_HW_ROW_LUT_PROTOTYPE
unsigned g_hw_bg_heading;
#endif
#endif
static uint8_t *hw_tile_frame;
static uint8_t *scene_pointer(uint8_t *frame,unsigned x,unsigned y) {
    if(!g_hw_tile_active) return frame+y*240+x;
#ifdef DOK_RENDER_SECTOR_BITMAP_PROTOTYPE
    return frame+y*128+(x>>1);
#else
    return frame+(y>>3)*1024+(x>>4)*64+(y&7)*8+((x>>1)&7);
#endif
}
#endif
static uint8_t *run_pointer(uint8_t *frame,unsigned x,unsigned y) {
#ifdef DOK_RENDER_HW_DIRECT_RUN_PROTOTYPE
    return scene_pointer(frame,x,y);
#else
    return frame+y*240+x;
#endif
}
static void store_pixel(uint8_t *dst,uint8_t color) {
#ifdef DOK_RENDER_HW_DIRECT_PROTOTYPE
    if(g_hw_tile_active) {
        unsigned offset=(unsigned)(dst-hw_tile_frame),y=offset/240,x=offset-y*240;
        *scene_pointer(hw_tile_frame,x,y)=color?color:(uint8_t)g_hw_tile_black;return;
    }
#endif
#ifdef DOK_RENDER_HALF_COLUMNS
    *(uint16_t *)(void *)dst=(uint16_t)(color|((unsigned)color<<8));
#else
    *dst=color;
#endif
}
static int field(const dok_runtime_level *l,uint16_t index,unsigned offset,int32_t *out) {
#ifdef DOK_RENDER_STATIC_SETUP_PROTOTYPE
    if(offset==0x54 && index<l->object_count) {*out=dok_static_insets[static_view][index];return 1;}
#endif
    const dok_runtime_definition *d;size_t j;
    if(index>=l->object_count) return 0;d=&l->definitions[index];
    for(j=0;j<d->field_count;++j) {
        const dok_runtime_field *f=&l->fields[d->field_begin+j];
        if(f->native_offset==offset && f->kind<=DOK_LEVEL_CHAR) {*out=f->value.number;return 1;}
    }
    return 0;
}
static int object_at(const dok_runtime *r,unsigned plane,int x,int z,uint16_t *out) {
    const dok_runtime_level *l=r->level;unsigned side=l->settings[DOK_LEVEL_WIDTH];uint16_t n;
    x=clamp(x,0,(int)side-1);z=clamp(z,0,(int)side-1);
    n=l->glyph_index[r->maps[plane*l->cells+(unsigned)z*side+(unsigned)x]];
    if(!n || n>r->object_count) return 0;*out=(uint16_t)(n-1);return 1;
}
static int neighbor_class(const dok_runtime *r,int x,int z,unsigned *out) {
#ifdef DOK_RENDER_STATIC_SETUP_PROTOTYPE
    (void)r;*out=dok_static_cells[static_view][0][clamp(z,0,63)*64+clamp(x,0,63)];return 1;
#else
    uint16_t n;if(!object_at(r,DOK_WORLD_TMAP,x,z,&n)) return 0;*out=r->level->definitions[n].class_id;return 1;
#endif
}
#ifdef DOK_RENDER_HW_GEOMETRY_PROFILE_PROTOTYPE
#define IMAGE_NAME image_geometry_impl
#else
#define IMAGE_NAME image
#endif
#ifdef DOK_RENDER_HW_HOT_IMAGE_PROTOTYPE
static DOK_NOINLINE dok_renderer_status IMAGE_NAME(dok_renderer_workspace *w,const dok_runtime *r,const dok_rom_assets *a,
#else
static DOK_COLD dok_renderer_status IMAGE_NAME(dok_renderer_workspace *w,const dok_runtime *r,const dok_rom_assets *a,
#endif
    unsigned level,unsigned language,uint16_t object) {
    uint16_t id;dok_rom_resource resource;dok_image_view v;const dok_runtime_definition *d=&r->level->definitions[object];
    if(w->image_known[object]) return DOK_RENDER_OK;
    int animated=(r->objects[object].flags&0x800)!=0 || d->class_id==8 || r->objects[object].visual_resource;
#ifdef DOK_RENDER_STATIC_SETUP_PROTOTYPE
    if(!animated && image_cache_view[object]==static_view+1) {w->image_known[object]=1;return DOK_RENDER_OK;}
#ifdef DOK_RENDER_HW_ANIM_CACHE_PROTOTYPE
    uint32_t animation_key=((uint32_t)r->objects[object].frame<<16)|r->objects[object].visual_resource;
    if(animated && image_cache_view[object]==((static_view+1)|128) && image_animation_key[object]==animation_key) {
        w->image_known[object]=1;return DOK_RENDER_OK;
    }
#endif
#endif
    id=r->objects[object].visual_resource;
    if((!id && dok_rom_binding(&id,a,level,language,object,animated?DOK_ROM_SLOT_ANIMATION:DOK_ROM_SLOT_SPRITE)!=DOK_ROM_OK) ||
       dok_rom_resource_at(&resource,a,id,language)!=DOK_ROM_OK) return DOK_RENDER_RESOURCE;
    if(animated) {
        dok_animation_view av;dok_animation_frame frame;
        if(dok_animation_open(resource.data,resource.size,&av)!=DOK_ASSET_OK ||
           dok_animation_frame_at(&av,r->objects[object].frame,&frame)!=DOK_ASSET_OK) return DOK_RENDER_RESOURCE;
        v=frame.image;
    } else if(dok_image_open(resource.data,resource.size,&v)!=DOK_ASSET_OK) return DOK_RENDER_RESOURCE;
    /* G05 never mutates shared resource images. Initial crop index0 produces
     * the raw pixels for the checked Sprite shift modes; animated/static-state
     * updates are deliberately outside this renderer's contract. */
    if(v.width>256 || v.height>512 || !v.width || !v.height) PROFILE_FAIL();
    w->images[object].pixels=v.pixels;w->images[object].width=v.width;w->images[object].height=v.height;
    w->images[object].flags=animated?8:(uint16_t)(v.stored_flags|d->sprite_flags);w->images[object].resource_id=id;
    w->images[object].columns=0;w->images[object].scales=0;
#ifdef DOK_RENDERER_PROBE
    if(!animated) {
    extern const uint8_t dok_render_bank[],dok_render_bank_end[];
    unsigned entry=((level*2+language)*(a->resource_count+1)+id)*8;
    const uint8_t *bp=dok_render_bank+entry;
    uint32_t off=(uint32_t)bp[0]|((uint32_t)bp[1]<<8)|((uint32_t)bp[2]<<16)|((uint32_t)bp[3]<<24);
    if(off) {
    if(off>(size_t)(dok_render_bank_end-dok_render_bank) ||
       (size_t)v.width*v.height>(size_t)(dok_render_bank_end-dok_render_bank)-off) return DOK_RENDER_RESOURCE;
    w->images[object].columns=dok_render_bank+off;
    uint32_t table=(uint32_t)bp[4]|((uint32_t)bp[5]<<8)|((uint32_t)bp[6]<<16)|((uint32_t)bp[7]<<24);
    if(table) {
        if((table&3) || table>(size_t)(dok_render_bank_end-dok_render_bank) ||
           v.height*4u>(size_t)(dok_render_bank_end-dok_render_bank)-table) return DOK_RENDER_RESOURCE;
        w->images[object].scales=(const uint32_t *)(const void *)(dok_render_bank+table);
    }
    }
    }
#endif
#ifdef DOK_RENDER_STATIC_SETUP_PROTOTYPE
#ifdef DOK_RENDER_HW_ANIM_CACHE_PROTOTYPE
    image_cache_view[object]=(uint8_t)((static_view+1)|(animated?128:0));
    if(animated)image_animation_key[object]=animation_key;
#else
    image_cache_view[object]=(uint8_t)(animated?0:static_view+1);
#endif
#endif
    w->image_known[object]=1;return DOK_RENDER_OK;
}
#ifdef DOK_RENDER_HW_GEOMETRY_PROFILE_PROTOTYPE
static DOK_COLD dok_renderer_status image(dok_renderer_workspace *w,const dok_runtime *r,const dok_rom_assets *a,
    unsigned level,unsigned language,uint16_t object) {
    uint32_t stamp=render_ticks();dok_renderer_status result=image_geometry_impl(w,r,a,level,language,object);
    g_geometry_work[6]+=render_ticks()-stamp;++g_geometry_work[7];return result;
}
#endif
#ifdef DOK_RENDER_RAY_WALLS_PROTOTYPE
DOK_COLD
#endif
void dok_renderer_camera_heading(dok_projection_camera *c,int heading) {
    int n=heading%360;if(n<0) n+=360;
#ifdef DOK_RENDER_HW_ROW_LUT_PROTOTYPE
    g_hw_bg_heading=(unsigned)n;
#endif
    c->sine=trig[n];c->cosine=trig[361+n];
}
#ifdef DOK_RENDER_PRELIT_PROTOTYPE
DOK_COLD
#endif
dok_renderer_status dok_renderer_camera_from_runtime(dok_projection_camera *out,const dok_runtime *r) {
    dok_projection_camera c;const dok_runtime_actor *a;uint16_t index;
    if(!out || !r || !r->level || !r->player_index || r->player_index>r->object_count) return DOK_RENDER_ARGUMENT;
    index=(uint16_t)(r->player_index-1);a=dok_runtime_actor_at(r,index);
    if(!a || !dok_runtime_position(r,index,&c.x,&c.y,&c.z)) PROFILE_FAIL();
    dok_renderer_camera_heading(&c,a->heading);c.distance=200;c.scale=8;c.focal=4096;c.center_x=159;c.center_y=79;
    *out=c;return DOK_RENDER_OK;
}
DOK_COLD dok_renderer_status dok_renderer_palette(uint8_t out[768],const dok_rom_assets *a,unsigned level,unsigned language) {
    uint16_t id;dok_rom_resource resource;unsigned i;
    if(!out || !a) return DOK_RENDER_ARGUMENT;
    if(dok_rom_binding(&id,a,level,language,DOK_ROM_GLOBAL_OBJECT,DOK_ROM_SLOT_PALETTE)!=DOK_ROM_OK ||
       dok_rom_resource_at(&resource,a,id,language)!=DOK_ROM_OK || resource.size!=768) return DOK_RENDER_RESOURCE;
    for(i=0;i<768;++i) {if(resource.data[i]>63) return DOK_RENDER_RESOURCE;}
    for(i=0;i<768;++i) out[i]=resource.data[i];return DOK_RENDER_OK;
}
/* Depth0 near clipping uses original low32 numerator and signed IDIV. */
static int clip_endpoint(dok_camera_point *a,const dok_camera_point *b,int32_t *u,int32_t other_u) {
    int32_t divisor=sub(b->depth,a->depth),num=mul(sub(a->lateral,b->lateral),b->depth);
    int32_t unum=mul(sub(other_u,*u),-a->depth);
    if(!divisor || (num==INT32_MIN && divisor==-1) || (unum==INT32_MIN && divisor==-1)) return 0;
    a->lateral=add(num/divisor,b->lateral);*u=add(*u,unum/divisor);a->depth=0;return 1;
}
#ifdef DOK_RENDER_GEOMETRY_CACHE_PROTOTYPE
#include "renderer_geometry_cache.inc"
#endif
static dok_renderer_status plane(dok_renderer_workspace *w,const dok_projection_camera *c,dok_renderer_stats *s,
    uint16_t object,int32_t x0,int32_t z0,int32_t x1,int32_t z1,int32_t y,int32_t height,
    int16_t v0,int centered) {
    dok_camera_point a,b;dok_screen_point at,ab,bt,bb;dok_renderer_face f;int32_t u0=0,u1=w->images[object].width-1;
#ifdef DOK_RENDER_GEOMETRY_CACHE_PROTOTYPE
    const geometry_vertex *va=geometry_transform(c,x0,y-height,z0,&a),*vb=geometry_transform(c,x1,y-height,z1,&b);
    if(va && va==vb) va=0; /* Direct-map collision replaced the first entry. */
#else
    dok_projection_transform(c,x0,y-height,z0,&a);dok_projection_transform(c,x1,y-height,z1,&b);
#endif
#ifdef DOK_RENDER_STREAM_BSP_PROTOTYPE
    int original_a=a.depth,original_b=b.depth;
#endif
    if(a.depth<=0 && b.depth<=0) {++s->faces_rejected;return DOK_RENDER_OK;}
    if(a.depth<0) {if(!clip_endpoint(&a,&b,&u0,u1)) return DOK_RENDER_PROJECTION;++s->near_clipped;
#ifdef DOK_RENDER_GEOMETRY_CACHE_PROTOTYPE
        va=0;
#endif
    }
    else if(b.depth<0) {if(!clip_endpoint(&b,&a,&u1,u0)) return DOK_RENDER_PROJECTION;++s->near_clipped;
#ifdef DOK_RENDER_GEOMETRY_CACHE_PROTOTYPE
        vb=0;
#endif
    }
#ifndef DOK_RENDER_REFERENCE
    /* Conservative horizontal frustum test, before division. With the fixed
     * native lens screenX=159+200*lateral/(depth+200). Two pixels of
     * margin cover the integer-factor/SAR rounding over this local envelope. */
    if((a.lateral*200 < -163*(a.depth+200) && b.lateral*200 < -163*(b.depth+200)) ||
       (a.lateral*200 > 163*(a.depth+200) && b.lateral*200 > 163*(b.depth+200))) {
        ++s->faces_rejected;return DOK_RENDER_OK;
    }
#endif
#ifdef DOK_RENDER_GEOMETRY_CACHE_PROTOTYPE
    if(!geometry_project(c,&a,va,&at) || !geometry_project(c,&b,vb,&bt)) return DOK_RENDER_PROJECTION;
#else
    if(!dok_projection_project(c,&a,&at) || !dok_projection_project(c,&b,&bt)) return DOK_RENDER_PROJECTION;
#endif
    a.vertical=add(a.vertical,height);b.vertical=add(b.vertical,height);
#ifdef DOK_RENDER_REFERENCE
    if(!dok_projection_project(c,&a,&ab) || !dok_projection_project(c,&b,&bb)) return DOK_RENDER_PROJECTION;
#else
    /* Bottom shares exactly the top's denominator and quotient. */
    ab.y=add(sar(mul(a.vertical,at.factor),15),c->center_y);
    bb.y=add(sar(mul(b.vertical,bt.factor),15),c->center_y);
#endif
    /* The G05 profile bounds scalar projections before multiplies in mapping
     * and affine scan setup. Extreme probes fail rather than overflow C. */
    if(at.x< -8192 || at.x>8192 || bt.x< -8192 || bt.x>8192 || at.y< -8192 || at.y>8192 ||
       ab.y< -8192 || ab.y>8192 || bt.y< -8192 || bt.y>8192 || bb.y< -8192 || bb.y>8192) PROFILE_FAIL();
    if(at.x==bt.x || (at.x<0 && bt.x<0) || (at.x>319 && bt.x>319) ||
       (ab.y<0 && bb.y<0) || (at.y>159 && bt.y>159)) {++s->faces_rejected;return DOK_RENDER_OK;}
    f.x0=scale_x(at.x);f.x1=scale_x(bt.x);f.top0=scale_y(at.y);f.top1=scale_y(bt.y);
    f.bottom0=scale_y(ab.y);f.bottom1=scale_y(bb.y);f.depth0=a.depth;f.depth1=b.depth;
    f.u0=u0;f.u1=u1;f.key=sar(add(a.depth,b.depth),1);
    if(centered) f.key-=sar(centered,2);
    if(f.key<0) f.key=0;f.image_index=object;f.v0=v0;
    if(f.x0==f.x1) {++s->faces_rejected;return DOK_RENDER_OK;}
#ifdef DOK_RENDER_STREAM_BSP_PROTOTYPE
    if(stream_active && stream_emitting) {++s->faces_built;stream_emit_face(w,&f,s);return DOK_RENDER_OK;}
#endif
#ifdef DOK_RENDER_SECTOR_CLIP_PROTOTYPE
    if(sector_active && sector_emitting){++s->faces_built;sector_static_face(w,&f,s);return DOK_RENDER_OK;}
#endif
    if(w->face_count>=DOK_RENDER_FACE_CAPACITY) return DOK_RENDER_CAPACITY;
#ifdef DOK_RENDER_STREAM_BSP_PROTOTYPE
    if(stream_active)stream_record(w->face_count,x0,z0,x1,z1,original_a,original_b);
#endif
    w->faces[w->face_count++]=f;++s->faces_built;return DOK_RENDER_OK;
}
#include "renderer_surfaces.inc"
#include "renderer_surface_sides.inc"
#include "renderer_sprites.inc"
#ifdef DOK_RENDER_WORLD_PLANES_PROTOTYPE
#include "renderer_world_planes.inc"
#endif
#ifdef DOK_RENDER_RAY_WALLS_PROTOTYPE
#include "renderer_ray_walls.inc"
#endif
#ifdef DOK_RENDER_BSP_PROTOTYPE
#include "renderer_bsp.inc"
#endif
#ifdef DOK_RENDER_HW_FC_SEED_PROTOTYPE
static DOK_COLD dok_renderer_status seed_hardware_surfaces(dok_renderer_workspace *w,const dok_runtime *r,
    const dok_rom_assets *a,unsigned level,unsigned language,const dok_projection_camera *c,const int bounds[4]) {
    if(!g_hw_tile_active)return DOK_RENDER_OK;
#ifdef DOK_RENDER_HW_NEAR_FC_PROTOTYPE
    const int distances[2]={1920,960};
#else
    const int distances[1]={1920};
#endif
    for(unsigned n=0;n<sizeof(distances)/sizeof(distances[0]);++n) {
        int distance=distances[n];
        int x=clamp((c->x-sar(c->sine*distance,15))/480,0,63),z=clamp((c->z+sar(c->cosine*distance,15))/480,0,63);
        if(x<bounds[0] || x>bounds[1] || z<bounds[2] || z>bounds[3])continue;
        uint16_t object;if(!object_at(r,DOK_WORLD_TMAP,x,z,&object))return DOK_RENDER_PROFILE;
        unsigned fc=dok_static_cells[static_view][1][z*64+x];
        if(r->level->definitions[object].class_id!=2 && dok_static_insets[static_view][fc]>=0 && dok_static_insets[static_view][fc]<=10) {
            dok_renderer_status status=surfaces_build(w,r,a,level,language,c,x,z,fc);if(status)return status;
            if(w->surface_known[fc][0] && w->surface_known[fc][1])return DOK_RENDER_OK;
        }
    }
    return DOK_RENDER_OK;
}
#endif
#ifdef DOK_RENDER_HW_STATIC_GEOMETRY_PROTOTYPE
#include "renderer_static_cache.inc"
#endif
static dok_renderer_status build(dok_renderer_workspace *w,const dok_runtime *r,const dok_rom_assets *a,
    unsigned level,unsigned language,const dok_projection_camera *c,dok_renderer_stats *s) {
    const dok_runtime_level *l=r->level;int side=l->settings[DOK_LEVEL_WIDTH],cell=l->settings[DOK_LEVEL_CELL_SIDE];
    /* Rotated four-corner G05 visibility envelope. Port policy, not yet
     * a reconstruction of every mixed-width original AABB instruction. */
    int xmin=side-1,xmax=0,zmin=side-1,zmax=0,x,z;
    const int lateral[4]={-1,1,-6,6},depth[4]={-2,-2,7,7};
    for(unsigned k=0;k<4;++k) {
        int dx=sar((lateral[k]*c->cosine-depth[k]*c->sine)*cell,15);
        int dz=sar((lateral[k]*c->sine+depth[k]*c->cosine)*cell,15);
        int cx=clamp((c->x+dx)/cell,0,side-1),cz=clamp((c->z+dz)/cell,0,side-1);
        if(cx<xmin) xmin=cx;if(cx>xmax) xmax=cx;
        if(cz<zmin) zmin=cz;if(cz>zmax) zmax=cz;
    }
#ifdef DOK_RENDER_RAY_WALLS_PROTOTYPE
    world_shared_envelope[0]=xmin;world_shared_envelope[1]=xmax;
    world_shared_envelope[2]=zmin;world_shared_envelope[3]=zmax;
#endif
#ifdef DOK_RENDER_HW_BG_PROTOTYPE
    g_hw_bg_envelope[0]=xmin;g_hw_bg_envelope[1]=xmax;g_hw_bg_envelope[2]=zmin;g_hw_bg_envelope[3]=zmax;
#endif
#ifdef DOK_RENDER_HW_STATIC_GEOMETRY_PROTOTYPE
    hw_static_envelope(w,(unsigned)(xmax-xmin+1)*(unsigned)(zmax-zmin+1),s);
#endif
#ifdef DOK_RENDER_HW_FC_SEED_PROTOTYPE
#ifdef DOK_RENDER_HW_STATIC_GEOMETRY_PROTOTYPE
    if(hw_static_mode!=HW_STATIC_REUSE)
#endif
    {const int bounds[4]={xmin,xmax,zmin,zmax};dok_renderer_status status=seed_hardware_surfaces(w,r,a,level,language,c,bounds);if(status)return status;}
#endif
    for(z=zmin;z<=zmax;++z) for(x=xmin;x<=xmax;++x) {
        uint16_t object,fc;const dok_runtime_definition *d;dok_renderer_status status;int32_t width,height,y;
        unsigned north,south,west,east;int32_t x0=x*cell,z0=z*cell;
        ++s->cells_considered;
        if(!object_at(r,DOK_WORLD_TMAP,x,z,&object)) PROFILE_FAIL();
#ifdef DOK_RENDER_STATIC_SETUP_PROTOTYPE
        fc=dok_static_cells[static_view][1][z*64+x];
#else
        if(!object_at(r,DOK_WORLD_FCMAP,x,z,&fc)) PROFILE_FAIL();
#endif
#ifdef DOK_RENDER_HW_CELL_CLASS_PROTOTYPE
        unsigned cls=l->definitions[object].class_id;
        int sprite_cell=cls==3 || cls==9 || cls==6 || cls==10 ||
            (dok_sprite_class_masks[static_view][r->maps[z*64+x]]&6);
#endif
#if defined(DOK_RENDER_BSP_PROTOTYPE) && !defined(DOK_RENDER_HW_BSP_REUSE_PROTOTYPE)
        if(bsp_hidden[(unsigned)z*8+((unsigned)x>>3)]&(1u<<(x&7))) {
            /* Keep the original sprite/AI consideration lease, even when
             * static geometry can be rejected before polygon construction. */
#ifdef DOK_RENDER_HW_CELL_CLASS_PROTOTYPE
            if(sprite_cell)
#endif
            {status=cell_sprites(w,r,a,level,language,c,s,x,z,fc,object);if(status) return status;}
            continue;
        }
#endif
#ifdef DOK_RENDER_STATIC_SETUP_PROTOTYPE
        if(dok_static_cells[static_view][2][z*64+x]) {
#endif
#ifdef DOK_RENDER_HW_STATIC_GEOMETRY_PROTOTYPE
        if(hw_static_mode!=HW_STATIC_REUSE) {
#endif
#ifdef RENDER_DEFER_STATIC
        if(RENDER_DEFER_STATIC)status=surface_image(w,r,a,level,language,fc,2);
        else
#endif
        {status=surface_sides(w,r,a,level,language,c,x,z,fc);}
        if(status) return status;
#ifdef DOK_RENDER_HW_STATIC_GEOMETRY_PROTOTYPE
        }
#endif
#ifdef DOK_RENDER_STATIC_SETUP_PROTOTYPE
        }
#endif
        d=&l->definitions[object];
        if(d->class_id!=2) {
#ifdef DOK_RENDER_HW_STATIC_GEOMETRY_PROTOTYPE
            if(hw_static_mode!=HW_STATIC_REUSE) {
#endif
#ifdef DOK_RENDER_WORLD_PLANES_PROTOTYPE
            status=world_plane_register(w,r,a,level,language,fc);if(status) return status;
#else
#ifdef DOK_RENDER_HW_NEAR_FC_PROTOTYPE
            status=surfaces_build_hardware(w,r,a,level,language,c,x,z,fc);if(status) return status;
#else
#ifdef RENDER_DEFER_STATIC
            if(!RENDER_DEFER_STATIC || !w->surface_known[fc][0] || !w->surface_known[fc][1]) {
#endif
            status=surfaces_build(w,r,a,level,language,c,x,z,fc);if(status) return status;
#ifdef RENDER_DEFER_STATIC
            }
            if(RENDER_DEFER_STATIC)w->surface_count=0;
#endif
#endif
#endif
#ifdef DOK_RENDER_HW_STATIC_GEOMETRY_PROTOTYPE
            }
#endif
        }
#ifdef DOK_RENDER_HW_CELL_CLASS_PROTOTYPE
        if(sprite_cell)
#endif
        {status=cell_sprites(w,r,a,level,language,c,s,x,z,fc,object);if(status) return status;}
        if(d->class_id!=2 && d->class_id!=4 && d->class_id!=7) {
            continue;
        }
#ifdef DOK_RENDER_HW_STATIC_GEOMETRY_PROTOTYPE
        if(d->class_id==2 && hw_static_mode==HW_STATIC_REUSE) {
            status=hw_static_restore_cell(w,(unsigned)((z-zmin)*(xmax-xmin+1)+x-xmin));if(status)return status;
            continue;
        }
        if(d->class_id==2 && hw_static_mode==HW_STATIC_CAPTURE)hw_static_wall_begin(w,s);
#endif
#ifdef DOK_RENDER_RAY_WALLS_PROTOTYPE
        if(ray_eligible[object]) continue;
#endif
        if(d->class_id==7 && r->objects[object].state.door.parameter_5c!=l->initial_objects[object].state.door.parameter_5c) PROFILE_FAIL();
        /* GBA presentation correction: native dimension/extent rounding leaves
         * a strip at the terminal open phase. Keep intermediate native crop,
         * but remove the panel completely once that phase is reached. Extent1
         * is the initial closed header, not a fully opened door. */
        if(d->class_id==7 && r->objects[object].state.door.extent>1 &&
           (int16_t)r->objects[object].frame>=r->objects[object].state.door.extent-1) continue;
#ifdef DOK_RENDER_HW_BSP_REUSE_PROTOTYPE
        if(d->class_id==2 && ((bsp_reused[z*8+(x>>3)]|bsp_hidden[z*8+(x>>3)])&(1u<<(x&7)))) {
            /* Keep the ordinary image consideration lease even if the BSP
             * skipped an occluded cell. Surface/sprite leases ran above. */
            status=image(w,r,a,level,language,object);if(status)return status;
            continue;
        }
#endif
        status=image(w,r,a,level,language,object);if(status) return status;
#ifdef RENDER_DEFER_STATIC
        if(RENDER_DEFER_STATIC && d->class_id==2) {++s->wall_cells;continue;}
#endif
        width=word((int32_t)w->images[object].width*d->width_parameter);
        height=word((int32_t)w->images[object].height*d->width_parameter);
        y=word(-(int32_t)l->definitions[fc].height_parameter);
        if(width<=0 || height<=0 || width>4096 || height>4096) PROFILE_FAIL();
#ifndef DOK_RENDER_REFERENCE
        {
            dok_camera_point mid;int32_t sa=c->sine<0?-c->sine:c->sine,ca=c->cosine<0?-c->cosine:c->cosine;
            int32_t radius=(width*(sa+ca)+65535)/65536+2;
            dok_projection_transform(c,x0+width/2,y,z0+width/2,&mid);
            int32_t far=mid.depth+radius;
            if(far<=0 || (mid.lateral-radius)*200>163*(far+200) ||
               (mid.lateral+radius)*200< -163*(far+200)) continue;
        }
#endif
        if(!neighbor_class(r,x,z-1,&north) || !neighbor_class(r,x,z+1,&south) ||
           !neighbor_class(r,x-1,z,&west) || !neighbor_class(r,x+1,z,&east)) PROFILE_FAIL();
        if(d->class_id==2) {
            const int nx[4]={0,-1,0,1},nz[4]={-1,0,1,0};
            unsigned neighbors[4];unsigned k;neighbors[0]=north;neighbors[1]=west;neighbors[2]=south;neighbors[3]=east;
            ++s->wall_cells;
            for(k=0;k<4;++k) if(neighbors[k]!=2) {
#ifdef DOK_RENDER_BACKFACE_PROTOTYPE
                /* Cull only complete opaque cell blocks. The depth+200 lens
                 * projects from an eye200 units behind the camera. Keep all
                 * faces when that eye is inside the block, and keep near-
                 * coplanar cases to cover the scalar projection's rounding. */
                int ex=c->x+sar(c->sine*200,15),ez=c->z-sar(c->cosine*200,15);
                if(width==480 && height==960 && !(w->images[object].flags&8) &&
                   !(c->x>=x0-4 && c->x<=x0+width+4 && c->z>=z0-4 && c->z<=z0+width+4) &&
                   !(ex>=x0-4 && ex<=x0+width+4 && ez>=z0-4 && ez<=z0+width+4)) {
                    if((k==0 && ez>z0+4) || (k==1 && ex>x0+4) ||
                       (k==2 && ez<z0+width-4) || (k==3 && ex<x0+width-4)) continue;
                }
#endif
                uint16_t nf;int32_t top_offset;int32_t ax=x0,az=z0,bx=x0,bz=z0,h,v;
                if(!object_at(r,DOK_WORLD_FCMAP,x+nx[k],z+nz[k],&nf) || !field(l,nf,0x54,&top_offset)) PROFILE_FAIL();
                h=word(height-top_offset+1);
                v=word(((int32_t)w->images[object].height*top_offset)/l->settings[DOK_LEVEL_CELL_HEIGHT]);
                if(h<=0) continue;
                if(k==0) bx+=width; /* orientation0 */
                else if(k==1) az+=width; /* orientation90 */
                else if(k==2) {ax+=width;az+=width;bz+=width;} /*180*/
                else {ax+=width;bx+=width;bz+=width;} /*270*/
                status=plane(w,c,s,object,ax,az,bx,bz,y,h,(int16_t)v,0);if(status) return status;
            }
#ifdef DOK_RENDER_HW_STATIC_GEOMETRY_PROTOTYPE
            if(hw_static_mode==HW_STATIC_CAPTURE)hw_static_wall_end(w,(unsigned)((z-zmin)*(xmax-xmin+1)+x-xmin),s);
#endif
        } else {
            int ns_open=(north!=2 && north!=4 && north!=7 && south!=2 && south!=4 && south!=7);
            int ew_open=(west!=2 && west!=4 && west!=7 && east!=2 && east!=4 && east!=7);
            ++s->center_cells;
            if(ns_open) {
                status=plane(w,c,s,object,x0,z0+width/2,x0+width,z0+width/2,y,height,0,width);if(status) return status;
            }
            if(ew_open) {
                status=plane(w,c,s,object,x0+width/2,z0,x0+width/2,z0+width,y,height,0,width);if(status) return status;
            }
        }
    }
    return DOK_RENDER_OK;
}
#if defined(DOK_RENDER_BSP_PROTOTYPE) || defined(DOK_RENDER_FAST_PLANES_PROTOTYPE) || defined(DOK_RENDER_PRELIT_PROTOTYPE) || defined(DOK_RENDER_WORLD_PLANES_PROTOTYPE)
static DOK_COLD void sort_faces(dok_renderer_workspace *w) {
#else
static void sort_faces(dok_renderer_workspace *w) {
#endif
#ifdef DOK_RENDERER_PROBE
    uint16_t *order=face_order;unsigned i;
#else
    uint16_t order[DOK_RENDER_FACE_CAPACITY];unsigned i;
#endif
    for(i=0;i<w->face_count;++i) {
        unsigned j=i;while(j && w->faces[order[j-1]].key<w->faces[i].key) {order[j]=order[j-1];--j;}
        order[j]=(uint16_t)i;
    }
    /* Apply stable permutation in cycles: move each 48-byte face once. */
#ifndef DOK_RENDERER_PROBE
    for(i=0;i<w->face_count;++i) if(order[i]!=i) {
        dok_renderer_face saved=w->faces[i];unsigned j=i;
        while(order[j]!=i) {unsigned next=order[j];w->faces[j]=w->faces[next];order[j]=(uint16_t)j;j=next;}
        w->faces[j]=saved;order[j]=(uint16_t)j;
    }
#endif
}
static void swap32(int32_t *a,int32_t *b) {int32_t n=*a;*a=*b;*b=n;}
#if defined(DOK_RENDER_RASTER_DIV_PROTOTYPE) || defined(DOK_RENDER_HW_ARITH_RECIP_PROTOTYPE)
extern int32_t dok_gba_plane_divide(int32_t,unsigned);
extern unsigned dok_gba_texture_quotient(unsigned,unsigned);
static int32_t raster_divide(int32_t n,unsigned d) {
    return d<=16384?dok_gba_plane_divide(n,d):n/(int32_t)d;
}
#define RASTER_DIV(n,d) raster_divide((n),(unsigned)(d))
#else
#define RASTER_DIV(n,d) ((n)/(d))
#endif
#ifdef DOK_RENDER_HW_VSTEP_DIV_PROTOTYPE
extern int32_t dok_gba_plane_divide(int32_t,unsigned);
#define VSTEP_DIV(n,d) ((unsigned)(d)-1<16384u?dok_gba_plane_divide((n),(unsigned)(d)):(n)/(d))
#else
#define VSTEP_DIV(n,d) RASTER_DIV(n,d)
#endif
#ifdef DOK_RENDER_HW_U_RECIP_PROTOTYPE
extern unsigned dok_gba_divide_u16(unsigned,unsigned);
#endif
#ifdef DOK_RENDER_HW_U_WIDE_PROTOTYPE
extern unsigned dok_gba_divide_positive32(unsigned,unsigned);
#endif
#ifndef DOK_RENDER_REFERENCE
typedef struct {unsigned quotient,remainder,step,carry,divisor;int sign;} edge_walk;
#ifdef DOK_RENDER_HW_EDGE_RAM_CALL_PROTOTYPE
static DOK_NOINLINE edge_walk edge_start(int delta,unsigned offset,unsigned span) {
#elif defined(DOK_RENDER_HW_EDGE_ROM_PROTOTYPE)
static DOK_COLD edge_walk edge_start(int delta,unsigned offset,unsigned span) {
#else
static edge_walk edge_start(int delta,unsigned offset,unsigned span) {
#endif
    edge_walk e;unsigned magnitude=(unsigned)(delta<0?-delta:delta),n=magnitude*offset;
#ifdef DOK_RENDER_RASTER_DIV_PROTOTYPE
    e.quotient=(unsigned)RASTER_DIV((int32_t)n,span);e.remainder=n-e.quotient*span;
    e.step=(unsigned)RASTER_DIV((int32_t)magnitude,span);e.carry=magnitude-e.step*span;
#else
    e.quotient=n/span;e.remainder=n%span;e.step=magnitude/span;e.carry=magnitude%span;
#endif
    e.divisor=span;e.sign=delta<0?-1:1;return e;
}
static void edge_next(edge_walk *e) {
    e->quotient+=e->step;e->remainder+=e->carry;
    if(e->remainder>=e->divisor) {e->remainder-=e->divisor;++e->quotient;}
}
#endif
#ifdef DOK_RENDER_RASTER_PROFILE_PROTOTYPE
#define MAPPED_COLUMN_NAME mapped_column_profile_impl
#else
#define MAPPED_COLUMN_NAME mapped_column
#endif
static DOK_NOINLINE unsigned MAPPED_COLUMN_NAME(uint8_t *restrict destination,const uint8_t *restrict pixels,
    unsigned v,unsigned step,unsigned count,unsigned masked) {
#ifdef DOK_RENDER_HW_DIRECT_PROTOTYPE
    if(g_hw_tile_active) {
        extern unsigned dok_gba_tiled_column(uint8_t *,const uint8_t *,unsigned,unsigned,unsigned,unsigned,unsigned);
#ifdef DOK_RENDER_HW_ROM_COLUMN_CACHE_PROTOTYPE
        extern unsigned dok_gba_tiled_column_cached(uint8_t *,const uint8_t *,unsigned,unsigned,unsigned,unsigned,unsigned);
#define HW_COLUMN_SAMPLER dok_gba_tiled_column_cached
#else
#define HW_COLUMN_SAMPLER dok_gba_tiled_column
#endif
        unsigned black=g_hw_tile_black;
#ifdef DOK_RENDER_HW_NONZERO_COLUMN_PROTOTYPE
        if(hw_column_nonzero)black=0; /* Source column contains no zero bytes. */
#endif
#ifdef DOK_RENDER_HW_DIRECT_RUN_PROTOTYPE
        (void)masked;
        return HW_COLUMN_SAMPLER(destination,pixels,v,step,count,((unsigned)(destination-hw_tile_frame)>>3)&7,black);
#else
        unsigned offset=(unsigned)(destination-hw_tile_frame),y=offset/240,x=offset-y*240;
        (void)masked;
        return HW_COLUMN_SAMPLER(scene_pointer(hw_tile_frame,x,y),pixels,v,step,count,y&7,black);
#endif
#undef HW_COLUMN_SAMPLER
    }
#endif
#ifdef DOK_RENDERER_PROBE
    extern unsigned dok_gba_mapped_column(uint8_t *,const uint8_t *,unsigned,unsigned,unsigned);
    (void)masked; /* Probe's masked fallback is handled in visible_column. */
    return dok_gba_mapped_column(destination,pixels,v,step,count);
#else
    unsigned written=0;
    if(masked) {
        for(unsigned n=0;n<count;++n,v+=step,destination+=240) {
            uint8_t color=pixels[v>>16];if(color) {*destination=color;++written;}
        }
    } else {
        unsigned n=0;
        /* Four independent stores amortize the loop branch without changing
         * any Q16 sample, including repeated texels and clipped starts. */
        for(;n+4<=count;n+=4,destination+=960) {
            destination[0]=pixels[v>>16];v+=step;
            destination[240]=pixels[v>>16];v+=step;
            destination[480]=pixels[v>>16];v+=step;
            destination[720]=pixels[v>>16];v+=step;
        }
        for(;n<count;++n,v+=step,destination+=240) *destination=pixels[v>>16];
        written=count;
    }
    return written;
#endif
}
#ifdef DOK_RENDER_RASTER_PROFILE_PROTOTYPE
static DOK_COLD unsigned mapped_column(uint8_t *destination,const uint8_t *pixels,unsigned v,unsigned step,unsigned count,unsigned masked) {
    uint32_t stamp=render_ticks();unsigned result=mapped_column_profile_impl(destination,pixels,v,step,count,masked);
    g_raster_work[2]+=render_ticks()-stamp;++g_raster_work[3];return result;
}
#endif
#if defined(DOK_RENDER_PRELIT_PROTOTYPE)
static DOK_COLD unsigned strip_column(uint8_t *restrict dst,const uint8_t *restrict src,unsigned count) {
#else
static DOK_NOINLINE unsigned strip_column(uint8_t *restrict dst,const uint8_t *restrict src,unsigned count) {
#endif
    unsigned n=count;
    while(n && ((uintptr_t)src&3)) {store_pixel(dst,*src++);dst+=240;--n;}
    while(n>=4) {
        uint32_t four=*(const uint32_t *)(const void *)src;src+=4;
        store_pixel(dst,(uint8_t)four);store_pixel(dst+240,(uint8_t)(four>>8));
        store_pixel(dst+480,(uint8_t)(four>>16));store_pixel(dst+720,(uint8_t)(four>>24));
        dst+=960;n-=4;
    }
    while(n--) {store_pixel(dst,*src++);dst+=240;}
    return count;
}
static unsigned draw_run(uint8_t *dst,const uint8_t *pixels,unsigned v,unsigned step,unsigned count,const uint8_t *strip) {
    return strip?strip_column(dst,strip,count):mapped_column(dst,pixels,v,step,count,0);
}
#ifdef DOK_RENDER_HW_HOT_COVER_PROTOTYPE
static DOK_NOINLINE void cover(dok_renderer_workspace *w,unsigned x,unsigned lo,unsigned hi) {
#elif defined(DOK_RENDER_HW_MASKED_PROTOTYPE)
static DOK_COLD void cover(dok_renderer_workspace *w,unsigned x,unsigned lo,unsigned hi) {
#else
static void cover(dok_renderer_workspace *w,unsigned x,unsigned lo,unsigned hi) {
#endif
    COVER_MARK(x,lo,hi);
    unsigned count=COVER_COUNT(w,x),a=0,b;
    while(a<count && (unsigned)COVER(w,x,a)[1]+1<lo) ++a;
    b=a;
    while(b<count && COVER(w,x,b)[0]<=hi+1) {
        if(COVER(w,x,b)[0]<lo) lo=COVER(w,x,b)[0];
        if(COVER(w,x,b)[1]>hi) hi=COVER(w,x,b)[1];++b;
    }
    if(b==a) {
        /* At most72 non-adjacent runs fit in144 rows. A new disjoint run
         * cannot exist once count72 is reached. */
        for(unsigned n=count;n>a;--n) {COVER(w,x,n)[0]=COVER(w,x,n-1)[0];COVER(w,x,n)[1]=COVER(w,x,n-1)[1];}
        ++count;
    } else {
        for(unsigned n=b;n<count;++n) {COVER(w,x,a+1+n-b)[0]=COVER(w,x,n)[0];COVER(w,x,a+1+n-b)[1]=COVER(w,x,n)[1];}
        count-=b-a-1;
    }
    COVER(w,x,a)[0]=(uint8_t)lo;COVER(w,x,a)[1]=(uint8_t)hi;COVER_COUNT(w,x)=(uint8_t)count;
}
#ifdef DOK_RENDER_RAY_WALLS_PROTOTYPE
#define VISIBLE_COLUMN_NAME visible_column_unclipped
#elif defined(DOK_RENDER_RASTER_PROFILE_PROTOTYPE)
#define VISIBLE_COLUMN_NAME visible_column_profile_impl
#else
#define VISIBLE_COLUMN_NAME visible_column
#endif
#if defined(DOK_RENDER_HW_ALPHA_EMPTY_PROTOTYPE) || defined(DOK_RENDER_HW_SPARSE_POSTS_PROTOTYPE)
#include "renderer_alpha.inc"
#endif
#ifdef DOK_RENDER_HW_SPARSE_POSTS_PROTOTYPE
#include "renderer_sparse_posts.inc"
#endif
#ifdef DOK_RENDER_HW_VISIBLE_SPLIT_PROTOTYPE
static DOK_COLD unsigned visible_column_legacy(dok_renderer_workspace *,unsigned,unsigned,unsigned,
    const uint8_t *,unsigned,unsigned,unsigned,const uint8_t *,unsigned);
#endif
static DOK_NOINLINE unsigned VISIBLE_COLUMN_NAME(dok_renderer_workspace *w,unsigned x,unsigned begin,unsigned end,
    const uint8_t *pixels,unsigned v,unsigned step,unsigned masked,const uint8_t *record,unsigned strip_first) {
#ifdef DOK_RENDER_HW_NONZERO_COLUMN_PROTOTYPE
    hw_column_nonzero=masked>>31;
#ifdef DOK_RENDER_HW_SPAN_SOLID_PROTOTYPE
    if(!hw_column_nonzero && (masked&0x40000000u)) {
        hw_column_nonzero=(unsigned)span_has_no_zero(hw_column_zero_bits,v,step,end-begin+1);
        if(hw_column_nonzero)masked=0;
    }
    masked&=0x3fffffffu;
#else
    masked&=0x7fffffffu;
#endif
#endif
#ifdef DOK_RENDER_SECTOR_CLIP_PROTOTYPE
    if(sector_dynamic && g_hw_tile_active && !record)return sector_dynamic_column(w,x,begin,end,pixels,v,step,masked);
#endif
#ifdef DOK_RENDER_HW_ALPHA_EMPTY_PROTOTYPE
    if(masked&0x20000000u) {
        masked&=~0x20000000u;
        if(g_hw_tile_active && !record) {
            unsigned available=COVER_COUNT(w,x)?hw_empty_column_count(x,begin,end):end-begin+1;
            COVER_COUNT(w,x)|=128;return available<<16;
        }
    }
#endif
#ifdef DOK_RENDER_HW_DIRECT_GAPS_PROTOTYPE
    if(g_hw_tile_active && !masked && !record) {
        if(!COVER_COUNT(w,x)) {
            unsigned result=draw_run(run_pointer(w->frame,x,begin),pixels,v,step,end-begin+1,0);
            COVER_MARK(x,begin,end);COVER_COUNT(w,x)=128;return result;
        }
        extern unsigned dok_gba_tiled_opaque_gaps(uint8_t *,const uint8_t *,unsigned,unsigned,unsigned,unsigned,unsigned,uint32_t *,unsigned);
        unsigned black=g_hw_tile_black;
#ifdef DOK_RENDER_HW_NONZERO_COLUMN_PROTOTYPE
        if(hw_column_nonzero)black=0;
#endif
#ifdef DOK_RENDER_HW_OPAQUE_BATCH_PROTOTYPE
        struct {uint8_t *dst;const uint8_t *pixels;unsigned v,step,count;uint32_t *bits;unsigned mask,tile_y,black;} span={
            scene_pointer(w->frame,x,begin),pixels,v,step,end-begin+1,&coverage_bits[x>>1][begin>>5],1u<<(begin&31),begin&7,black};
        extern unsigned dok_gba_tiled_opaque_batch(const void *);
        unsigned result=dok_gba_tiled_opaque_batch(&span);
#else
        unsigned result=dok_gba_tiled_opaque_gaps(scene_pointer(w->frame,x,begin),pixels,v,step,end-begin+1,begin&7,black,
            &coverage_bits[x>>1][begin>>5],begin&31);
#endif
        COVER_COUNT(w,x)=128;return result;
    }
#endif
#ifdef DOK_RENDER_HW_MASKED_PROTOTYPE
    if(g_hw_tile_active && masked && !record) {
#ifdef DOK_RENDER_HW_SPARSE_POSTS_PROTOTYPE
        if(hw_column_posts) {
            unsigned result=sparse_column(w,x,begin,end,pixels,v,step,hw_column_posts);
            COVER_COUNT(w,x)|=128;return result;
        }
#endif
        struct {uint8_t *dst;const uint8_t *pixels;unsigned v,step,count;uint32_t *bits;unsigned mask,tile_y;} span={
            scene_pointer(w->frame,x,begin),pixels,v,step,end-begin+1,&coverage_bits[x>>1][begin>>5],1u<<(begin&31),begin&7};
        extern unsigned dok_gba_tiled_masked_column(const void *);
        unsigned result=dok_gba_tiled_masked_column(&span);COVER_COUNT(w,x)|=128;return result;
    }
#endif
#ifdef DOK_RENDER_MASKED_ARM_PROTOTYPE
    if(masked && !record) {
        struct {uint8_t *dst;const uint8_t *pixels;unsigned v,step,count;uint32_t *bits;unsigned mask;} span={
            w->frame+begin*240+x,pixels,v,step,end-begin+1,&coverage_bits[x>>1][begin>>5],1u<<(begin&31)};
        extern unsigned dok_gba_masked_column(const void *);
        unsigned result=dok_gba_masked_column(&span);
        COVER_COUNT(w,x)|=128;return result;
    }
#endif
#ifdef DOK_RENDER_HW_VISIBLE_SPLIT_PROTOTYPE
    return visible_column_legacy(w,x,begin,end,pixels,v,step,masked,record,strip_first);
}
static DOK_COLD unsigned visible_column_legacy(dok_renderer_workspace *w,unsigned x,unsigned begin,unsigned end,
    const uint8_t *pixels,unsigned v,unsigned step,unsigned masked,const uint8_t *record,unsigned strip_first) {
#endif
    uint8_t gaps[73][2];unsigned n=0,start=begin,written=0,available=0,count=COVER_COUNT(w,x);
    const uint8_t *strip=record?record+record[1]+strip_first:0;
    /* The ordinary wall case has one continuous covered interval. Avoid
     * materializing a gap list and calling the general interval merger. */
    if(!masked) {
        if(!count) {
            draw_run(run_pointer(w->frame,x,begin),pixels,v,step,end-begin+1,strip);
            COVER_MARK(x,begin,end);
            COVER(w,x,0)[0]=(uint8_t)begin;COVER(w,x,0)[1]=(uint8_t)end;
            COVER_COUNT(w,x)=1;return end-begin+1;
        }
        unsigned lo=COVER(w,x,0)[0],hi=COVER(w,x,0)[1];
        if(count==1 && begin<=hi+1 && end+1>=lo) {
            COVER_MARK(x,begin,end);
            if(begin<lo) {
                unsigned last=end<lo?end:lo-1;
                written+=draw_run(run_pointer(w->frame,x,begin),pixels,v,step,last-begin+1,strip);
                COVER(w,x,0)[0]=(uint8_t)begin;
            }
            if(end>hi) {
                unsigned first=begin>hi?begin:hi+1;
                written+=draw_run(run_pointer(w->frame,x,first),pixels,v+(first-begin)*step,step,end-first+1,strip?strip+(first-begin):0);
                COVER(w,x,0)[1]=(uint8_t)end;
            }
            return written;
        }
    }
    for(unsigned k=0;k<count && start<=end;++k) {
        unsigned lo=COVER(w,x,k)[0],hi=COVER(w,x,k)[1];
        if(hi<start) continue;if(lo>end) break;
        if(lo>start) {gaps[n][0]=(uint8_t)start;gaps[n++][1]=(uint8_t)(lo-1);}
        start=hi+1;
    }
    if(start<=end) {gaps[n][0]=(uint8_t)start;gaps[n++][1]=(uint8_t)end;}
    for(unsigned k=0;k<n;++k) {
        unsigned lo=gaps[k][0],hi=gaps[k][1],pos=v+(lo-begin)*step;available+=hi-lo+1;
        uint8_t *dst=w->frame+lo*240+x;
        if(!masked) {
            written+=draw_run(run_pointer(w->frame,x,lo),pixels,pos,step,hi-lo+1,strip?strip+(lo-begin):0);cover(w,x,lo,hi);
        } else if(record) {
            for(unsigned j=0;j<record[0];++j) {
                int first=(int)begin+(int)record[4+j*2]-(int)strip_first;
                int last=(int)begin+(int)record[5+j*2]-(int)strip_first;
                if(last<(int)lo) continue;if(first>(int)hi) break;
                if(first<(int)lo) first=(int)lo;if(last>(int)hi) last=(int)hi;
                written+=strip_column(w->frame+(unsigned)first*240+x,strip+((unsigned)first-begin),(unsigned)(last-first+1));
                cover(w,x,(unsigned)first,(unsigned)last);
            }
        } else {
            unsigned run=144;
            for(unsigned y=lo;y<=hi;++y,pos+=step,dst+=240) {
                uint8_t color=pixels[pos>>16];
                if(color) {store_pixel(dst,color);++written;if(run==144) run=y;}
                else if(run!=144) {cover(w,x,run,y-1);run=144;}
            }
            if(run!=144) cover(w,x,run,hi);
        }
    }
    return written|((available-written)<<16);
}
#ifdef DOK_RENDER_RASTER_PROFILE_PROTOTYPE
static DOK_COLD unsigned visible_column(dok_renderer_workspace *w,unsigned x,unsigned begin,unsigned end,
    const uint8_t *pixels,unsigned v,unsigned step,unsigned masked,const uint8_t *record,unsigned strip_first) {
    uint32_t stamp=render_ticks();unsigned result=visible_column_profile_impl(w,x,begin,end,pixels,v,step,masked,record,strip_first);
    g_raster_work[0]+=render_ticks()-stamp;++g_raster_work[1];return result;
}
#endif
#ifdef DOK_RENDER_RAY_WALLS_PROTOTYPE
static unsigned visible_column(dok_renderer_workspace *w,unsigned x,unsigned begin,unsigned end,
    const uint8_t *pixels,unsigned v,unsigned step,unsigned masked,const uint8_t *record,unsigned strip_first) {
#ifdef DOK_RENDER_RAY_WALLS_PROTOTYPE
    if(ray_clip_lo<=ray_clip_hi && (int)end>=ray_clip_lo && (int)begin<=ray_clip_hi) {
        unsigned n=0;
        if((int)begin<ray_clip_lo) n+=visible_column_unclipped(w,x,begin,(unsigned)(ray_clip_lo-1),pixels,v,step,masked,record,strip_first);
        if((int)end>ray_clip_hi) {
            unsigned first=(unsigned)(ray_clip_hi+1),offset=first-begin;
            n+=visible_column_unclipped(w,x,first,end,pixels,v+offset*step,step,masked,record,strip_first+offset);
        }
        return n;
    }
#endif
    return visible_column_unclipped(w,x,begin,end,pixels,v,step,masked,record,strip_first);
}
#endif
static void raster(dok_renderer_workspace *,const dok_runtime *,const dok_renderer_face *,const uint8_t *,dok_renderer_stats *);
#include "renderer_surface_raster.inc"
#ifdef DOK_RENDER_HW_FACE_BOX_PROTOTYPE
volatile uint32_t g_hw_face_box[2]; /* cumulative tests/rejected faces */
static DOK_COLD int face_box_covered(const dok_renderer_face *f,unsigned first,unsigned last) {
    ++g_hw_face_box[0];
    int top=f->top0<f->top1?f->top0:f->top1,bottom=f->bottom0>f->bottom1?f->bottom0:f->bottom1;
    if(bottom<0 || top>=DOK_RENDER_SCENE_HEIGHT || bottom<top){++g_hw_face_box[1];return 1;}
    unsigned lo=(unsigned)clamp(top,0,DOK_RENDER_SCENE_HEIGHT-1),hi=(unsigned)clamp(bottom,0,DOK_RENDER_SCENE_HEIGHT-1);
    unsigned a=lo>>5,b=hi>>5;uint32_t ma=UINT32_MAX<<(lo&31),mb=UINT32_MAX>>(31-(hi&31));
    if(a==b) {
        ma&=mb;
        for(unsigned x=first;x<=last;x+=2)if((coverage_bits[x>>1][a]&ma)!=ma)return 0;
    } else for(unsigned x=first;x<=last;x+=2) {
        const uint32_t *bits=coverage_bits[x>>1];
        if((bits[a]&ma)!=ma || (bits[b]&mb)!=mb)return 0;
        for(unsigned k=a+1;k<b;++k)if(bits[k]!=UINT32_MAX)return 0;
    }
    ++g_hw_face_box[1];return 1;
}
#endif
#ifdef DOK_RENDER_HW_RECT_RASTER_PROTOTYPE
#include "renderer_rect.inc"
#endif
#ifdef DOK_RENDER_HW_WORLD_OBJ_PROTOTYPE
#include "renderer_world_obj.inc"
#endif
#if defined(DOK_RENDER_FAST_OPAQUE_PROTOTYPE) || defined(DOK_RENDER_FAST_OPAQUE_ARM_PROTOTYPE)
static DOK_COLD void raster_general(dok_renderer_workspace *w,const dok_runtime *r,const dok_renderer_face *original,const uint8_t *light,dok_renderer_stats *s) {
#elif defined(DOK_RENDER_COLUMN_PIPELINE_PROTOTYPE) || defined(DOK_RENDER_SECTOR_CLIP_PROTOTYPE)
static DOK_COLD void raster(dok_renderer_workspace *w,const dok_runtime *r,const dok_renderer_face *original,const uint8_t *light,dok_renderer_stats *s) {
#else
static void raster(dok_renderer_workspace *w,const dok_runtime *r,const dok_renderer_face *original,const uint8_t *light,dok_renderer_stats *s) {
#endif
    (void)light;
    dok_renderer_face f=*original;const dok_renderer_image *im=face_image(w,f.image_index);
    int x,first,last,span,q0,q1;int wrote=0;unsigned pixels=0,transparent=0;
    if(f.x0>f.x1) {
        swap32(&f.x0,&f.x1);swap32(&f.top0,&f.top1);swap32(&f.bottom0,&f.bottom1);
        swap32(&f.depth0,&f.depth1);swap32(&f.u0,&f.u1);
    }
    span=(int)(f.x1-f.x0);first=clamp((int)f.x0,0,239);last=clamp((int)f.x1,0,239);
    if(span<=0 || f.x1<0 || f.x0>239) return;
#ifdef DOK_RENDER_HALF_COLUMNS
    first=(first+1)&~1;
#endif
#ifdef DOK_RENDER_STREAM_BSP_PROTOTYPE
    if(stream_active) {
        if(first<(int)stream_clip_first)first=(int)((stream_clip_first+1)&~1u);
        if(last>(int)stream_clip_last)last=(int)stream_clip_last;
        if(first>last)return;
    }
#endif
#ifdef DOK_RENDER_HW_FACE_BOX_PROTOTYPE
#ifdef DOK_RENDER_SECTOR_CLIP_PROTOTYPE
    if(!sector_dynamic)
#endif
    if(face_box_covered(&f,(unsigned)first,(unsigned)last))return;
#endif
#ifdef DOK_RENDER_HW_RECT_RASTER_PROTOTYPE
#ifdef DOK_RENDER_HW_WORLD_OBJ_PROTOTYPE
    if(g_hw_tile_active)world_obj_face(w,&f,im,g_world_obj_state->stride-1,s);
#endif
    if(g_hw_tile_active &&
       (f.image_index>=r->object_count || r->level->definitions[f.image_index].class_id!=7) &&
       raster_rect(w,&f,im,s))return;
#endif
    /* Bounded Q7 reciprocal approximation is a G05 raster choice. Projection,
     * face vertices and depth remain G04 exact. Constant-depth doors retain
     * exact affine U; complete original interpolation belongs to G11. */
    q0=RASTER_DIV(6553600,f.depth0+200)>>8;q1=RASTER_DIV(6553600,f.depth1+200)>>8;
    if(q0<1) q0=1;if(q1<1) q1=1;
#ifdef DOK_RENDER_HW_COLUMN_CACHE_PROTOTYPE
    unsigned cached_light=16;
    if(f.depth0>=0 && f.depth1>=0 && f.depth0<32768 && f.depth1<32768) {
#ifdef DOK_RENDER_HW_FACE_LIGHT_PROTOTYPE
        cached_light=light_index((f.depth0+f.depth1)/2);
#else
        unsigned a=light_index(f.depth0),b=light_index(f.depth1);
        if(a==b)cached_light=a;
#endif
    }
    int cached_height=-1,cached_step=0;
#endif
#ifdef DOK_RENDER_HW_AFFINE_U_PROTOTYPE
    int affine_u=q0==q1 && f.u0>=0 && f.u1>=0 && span<=16384 && q0<=128 && last-first>=14;
    edge_walk uv;
    if(affine_u)uv=edge_start(f.u1-f.u0,(unsigned)(first-f.x0),(unsigned)span);
#endif
#ifndef DOK_RENDER_REFERENCE
    /* i is nonnegative after clipping X. Accumulate abs(delta)*i/span
     * with quotient/remainder, then restore the sign for exact trunc0. */
    edge_walk upper=edge_start(f.top1-f.top0,(unsigned)(first-f.x0),(unsigned)span);
    edge_walk lower=edge_start(f.bottom1-f.bottom0,(unsigned)(first-f.x0),(unsigned)span);
#endif
#ifdef DOK_RENDER_HW_U_TRACK_PROTOTYPE
    int tracked_u=0,tracked_r=0,tracked_i=-1;
    const int ud=q1-q0,un=f.u1*q1-f.u0*q0;
    const int trackable=f.u0>=0 && f.u1>=0 && f.u0<=512 && f.u1<=512 && q0<=128 && q1<=128 && span<=16384;
#endif
    for(x=first;x<=last;
#ifdef DOK_RENDER_HALF_COLUMNS
        x+=2
#else
        ++x
#endif
    ) {
#ifdef DOK_RENDER_HW_AFFINE_U_PROTOTYPE
        int sampled_u=0;
        if(affine_u) {
            sampled_u=f.u0+uv.sign*(int)uv.quotient-(uv.sign<0 && uv.remainder!=0);
            edge_next(&uv);edge_next(&uv);
        }
#endif
#ifdef DOK_RENDER_REFERENCE
        int i=x-(int)f.x0,top=(int)(f.top0+(f.top1-f.top0)*i/span);
        int bottom=(int)(f.bottom0+(f.bottom1-f.bottom0)*i/span);
#else
        int i=x-(int)f.x0,top=f.top0+upper.sign*(int)upper.quotient;
        int bottom=f.bottom0+lower.sign*(int)lower.quotient;
        edge_next(&upper);edge_next(&lower);
#ifdef DOK_RENDER_HALF_COLUMNS
        edge_next(&upper);edge_next(&lower);
#endif
#endif
        int begin,end,denom,u,vstart,vstep;
        int32_t numerator;

        if(bottom<top || bottom<0 || top>=DOK_RENDER_SCENE_HEIGHT) continue;
        begin=clamp(top,0,DOK_RENDER_SCENE_HEIGHT-1);end=clamp(bottom,0,DOK_RENDER_SCENE_HEIGHT-1);
#ifdef DOK_RENDER_STREAM_BSP_PROTOTYPE
        if(stream_active && stream_vertical_clip) {
            int depth=f.depth0+RASTER_DIV((f.depth1-f.depth0)*i,span);
            int factor=6553600/(depth+200);
            int limit=scale_y(sar(mul(stream_ceiling-stream_camera->y,factor),15)+79);
            if(begin<limit)begin=limit;
            if(begin>end)continue;
        }
#endif
#ifdef DOK_RENDER_RAY_WALLS_PROTOTYPE
        ray_clip_lo=144;ray_clip_hi=-1;
        int static_depth=f.depth0+(f.depth1-f.depth0)*i/span;
        if(ray_wall[(unsigned)x>>1].depth!=INT_MAX && static_depth>ray_wall[(unsigned)x>>1].depth+1) {
            ray_clip_lo=ray_wall[(unsigned)x>>1].top;ray_clip_hi=ray_wall[(unsigned)x>>1].bottom;
        }
#endif
#if defined(DOK_RENDERER_PROBE) && defined(DOK_RENDER_HALF_COLUMNS)
        if(
#ifdef DOK_RENDER_SECTOR_CLIP_PROTOTYPE
            !sector_dynamic &&
#endif
            coverage_contains((unsigned)x,(unsigned)begin,(unsigned)end)) continue;
#ifdef DOK_RENDER_HW_DIRECT_GAPS_PROTOTYPE
        if(!g_hw_tile_active)coverage_sync(w,(unsigned)x);
#else
        coverage_sync(w,(unsigned)x);
#endif
#else
        unsigned occluded=0;
        for(unsigned k=0;k<COVER_COUNT(w,x);++k)
            if(COVER(w,x,k)[0]<=begin && COVER(w,x,k)[1]>=end) {occluded=1;break;}
        if(occluded) continue;
#endif
#ifdef DOK_RENDER_HW_AFFINE_U_PROTOTYPE
        if(affine_u)u=sampled_u;
        else {
#endif
        denom=q0*(span-i)+q1*i;
#ifndef DOK_RENDER_HW_U_TRACK_PROTOTYPE
        numerator=f.u0*q0*(span-i)+f.u1*q1*i;
#endif
#ifdef DOK_RENDER_HW_U_TRACK_PROTOTYPE
        if(trackable) {
            int ready=0;
            if(tracked_i>=0) {
                int rem=tracked_r+(un-tracked_u*ud)*(i-tracked_i),quotient=tracked_u;
                if(rem<0) {
                    --quotient;rem+=denom;
                    if(rem<0) {--quotient;rem+=denom;}
                } else if(rem>=denom) {
                    ++quotient;rem-=denom;
                    if(rem>=denom) {++quotient;rem-=denom;}
                }
                if(rem>=0 && rem<denom) {u=quotient;tracked_r=rem;ready=1;}
            }
            if(!ready) {numerator=f.u0*q0*(span-i)+f.u1*q1*i;u=numerator/denom;tracked_r=numerator-u*denom;}
            tracked_u=u;tracked_i=i;
        } else {
            numerator=f.u0*q0*(span-i)+f.u1*q1*i;
            u=denom?numerator/denom:0;
        }
#else
#ifdef DOK_RENDER_HW_AFFINE_DIV_PROTOTYPE
        if(q0==q1 && f.u1>=f.u0 && span<=16384)
            u=f.u0+dok_gba_plane_divide((f.u1-f.u0)*i,(unsigned)span);
        else
#endif
#ifdef DOK_RENDER_HW_U_WIDE_PROTOTYPE
        if(numerator>=0 && denom>0)u=(int)dok_gba_divide_positive32((unsigned)numerator,(unsigned)denom);
        else
#endif
#ifdef DOK_RENDER_HW_U_RECIP_PROTOTYPE
        if(numerator>=0 && (unsigned)denom-1<65535u)u=(int)dok_gba_divide_u16((unsigned)numerator,(unsigned)denom);
        else
#endif
#ifdef DOK_RENDER_RASTER_DIV_PROTOTYPE
        if(numerator>=0 && denom>0 && (unsigned)denom<=0x01ffffffu && ((unsigned)numerator>>8)<(unsigned)denom)
            u=(int)dok_gba_texture_quotient((unsigned)numerator,(unsigned)denom);
        else
#endif
        u=denom?numerator/denom:0;
#endif
#ifdef DOK_RENDER_HW_AFFINE_U_PROTOTYPE
        }
#endif
        u=clamp(u,0,im->width-1);
        /* Door owns its crop header, while the original payload stays shared.
         * LEVEL door sprites shift right (0x48) or down (0x108). Integer
         * division precedes multiplication, as native crop transform. */
        int crop_y=0;
        if(f.image_index<r->object_count && r->level->definitions[f.image_index].class_id==7) {
            const dok_runtime_object *o=&r->objects[f.image_index];
            int divisor=o->state.door.extent;
            if(divisor<1) divisor=1;
            if(im->flags&0x40) {
                int shift=(im->width/divisor)*(int16_t)o->frame;
                if(u<shift) continue;
                u-=shift;
            } else if(im->flags&0x100) crop_y=(im->height/divisor)*(int16_t)o->frame;
        }
        vstart=clamp(f.v0,0,im->height-1);
#ifdef DOK_RENDERER_PROBE
        const uint8_t *strip=0;
#if !defined(DOK_RENDER_NO_SCALES_PROTOTYPE) && !defined(DOK_RENDER_NO_SCALES_RUNTIME_PROTOTYPE)
        if(!crop_y && im->scales && im->scales[vstart] && bottom>top) {
            extern const uint8_t dok_render_bank[];
            const uint32_t *spans=(const uint32_t *)(const void *)(dok_render_bank+im->scales[vstart]);
            if((unsigned)(bottom-top)<=spans[0]) {
                const uint32_t *columns=(const uint32_t *)(const void *)(dok_render_bank+spans[bottom-top]);
                strip=dok_render_bank+columns[u];
            }
        }
#endif
        if(strip) {vstart=0;vstep=0;} else
#endif
        {
#ifdef DOK_RENDER_HW_COLUMN_CACHE_PROTOTYPE
        if(bottom-top==cached_height)vstep=cached_step;
        else {vstep=bottom>top?VSTEP_DIV((im->height-1-vstart)<<16,bottom-top):0;cached_height=bottom-top;cached_step=vstep;}
#else
        vstep=bottom>top?VSTEP_DIV((im->height-1-vstart)<<16,bottom-top):0;
#endif
        vstart=(vstart<<16)+(begin-top)*vstep;
        if(crop_y) {
            int threshold=crop_y<<16;
            if(vstart<threshold) {
                if(!vstep) continue;
                int skip=(threshold-vstart+vstep-1)/vstep;
                begin+=skip;vstart+=skip*vstep;
                if(begin>end) continue;
            }
            vstart-=threshold;
        }
        }
        unsigned row=
#ifdef DOK_RENDER_HW_COLUMN_CACHE_PROTOTYPE
            cached_light<16?cached_light:
#endif
            light_index(f.depth0+RASTER_DIV((f.depth1-f.depth0)*i,span));
#ifdef DOK_RENDER_PRELIT_PROTOTYPE
        /* Derived ROM columns retain source dimensions and transparency.
         * Five light bands replace per-output-pixel LIGHT preparation. */
        extern const uint8_t dok_prelit_bank[];
#ifdef DOK_RENDER_EXACT_LIGHT_PROTOTYPE
        unsigned band=row;
        uint32_t prelit=im->resource_id?prelit_view[im->resource_id*16+band]:0;
#else
        unsigned band=(row+2)/4;if(band>4) band=4;
        uint32_t prelit=im->resource_id?prelit_view[im->resource_id*5+band]:0;
#endif
        if(im->columns && prelit) {
            unsigned mode=PRELIT_COLUMN_MODE(im->flags,prelit,(unsigned)u);
#ifdef DOK_RENDER_HW_SPARSE_POSTS_PROTOTYPE
            SET_COLUMN_POSTS(im,prelit,(unsigned)u);
#endif
#ifdef DOK_RENDER_HW_SPAN_SOLID_PROTOTYPE
            hw_column_zero_bits=((const uint32_t *)(const void *)(dok_prelit_bank+prelit-256-im->width*4))[(unsigned)u];
            mode|=0x40000000u;
#endif
            unsigned n=visible_column(w,(unsigned)x,(unsigned)begin,(unsigned)end,
                dok_prelit_bank+prelit+(unsigned)u*im->height,
                (unsigned)vstart,(unsigned)vstep,
                mode,0,0);
            transparent+=n>>16;n&=65535u;pixels+=n;if(n) wrote=1;
            continue;
        }
#endif
        unsigned n;
#ifdef DOK_RENDER_HW_SPARSE_POSTS_PROTOTYPE
        hw_column_posts=0;
#endif
#ifdef DOK_RENDERER_PROBE
        if(!row && im->columns) n=visible_column(w,(unsigned)x,(unsigned)begin,(unsigned)end,im->columns+(unsigned)u*im->height,
            (unsigned)vstart,(unsigned)vstep,COLUMN_MODE(im->flags,im->columns[(int)u-256]),strip,(unsigned)(begin-top));
        else
#endif
        {
            uint8_t lit[144];const uint8_t *lut=w->lights+row*256;
            for(unsigned k=0;k<(unsigned)(end-begin+1);++k) {
#ifdef DOK_RENDERER_PROBE
                unsigned texel=strip?strip[strip[1]+(unsigned)(begin-top)+k]:(im->columns?
                    im->columns[(unsigned)u*im->height+((unsigned)(vstart+(int)k*vstep)>>16)]:
                    im->pixels[((unsigned)(vstart+(int)k*vstep)>>16)*im->width+(unsigned)u]);
#else
                unsigned texel=im->pixels[((unsigned)(vstart+(int)k*vstep)>>16)*im->width+(unsigned)u];
#endif
                lit[k]=lut[texel];
            }
            n=visible_column(w,(unsigned)x,(unsigned)begin,(unsigned)end,lit,0,65536,im->flags&8u,0,0);
        }
        transparent+=n>>16;n&=65535u;
        pixels+=n;if(n) wrote=1;
    }
    s->pixels_written+=pixels;s->transparent_pixels+=transparent;
    if(wrote) ++s->faces_drawn;
}
#ifdef DOK_RENDER_FAST_OPAQUE_PROTOTYPE
#include "renderer_opaque.inc"
#endif
#ifdef DOK_RENDER_FAST_OPAQUE_ARM_PROTOTYPE
#include "renderer_opaque_arm.inc"
#endif
#ifdef DOK_RENDER_HW_LOW_ADAPTIVE_PROTOTYPE
#include "renderer_low_choice.inc"
#endif
#ifdef DOK_RENDER_STREAM_BSP_PROTOTYPE
#include "renderer_stream.inc"
#endif
#ifdef DOK_RENDER_COLUMN_PIPELINE_PROTOTYPE
#include "renderer_columns.inc"
#endif
static void clear_rows(dok_renderer_workspace *w,const dok_runtime_level *level,unsigned first) {
    (void)level;
#ifdef DOK_RENDER_HW_DIRECT_PROTOTYPE
    if(g_hw_tile_active) {
        uint32_t color=g_hw_tile_black*0x01010101u;
#ifdef DOK_RENDER_HW_BG_PROTOTYPE
        color=0; /* The hardware horizontal planes remain behind foreground. */
#endif
        for(unsigned i=0;i<18432/4;i+=4) {
            w->frame_words[i]=color;w->frame_words[i+1]=color;w->frame_words[i+2]=color;w->frame_words[i+3]=color;
        }
        first=144;
    }
#endif
    for(unsigned y=first;y<160;++y) {
        uint32_t color=0; /* native world draw resets current color to index0 */
        uint32_t *row=w->frame_words+y*60;
        color*=0x01010101u;
        for(unsigned x=0;x<60;x+=4) {row[x]=color;row[x+1]=color;row[x+2]=color;row[x+3]=color;}
    }
}
#ifdef DOK_RENDERER_PROBE
#if defined(DOK_RENDER_RAY_WALLS_PROTOTYPE) || defined(DOK_RENDER_HW_DIRECT_PROTOTYPE)
static DOK_COLD int defer_background(const dok_renderer_workspace *w) {
#else
static int defer_background(const dok_renderer_workspace *w) {
#endif
    /* A large unmasked plane guarantees substantial coverage. The estimate
     * only selects a fill strategy; actual uncovered intervals are filled
     * after raster, so visibility and sampling never depend on this heuristic. */
    for(unsigned i=0;i<w->face_count;++i) {
        const dok_renderer_face *f=&w->faces[i];
        if(face_image(w,f->image_index)->flags&8u) continue;
        int top=f->top0>f->top1?f->top0:f->top1;
        int bottom=f->bottom0<f->bottom1?f->bottom0:f->bottom1;
        int width=clamp(f->x1,0,239)-clamp(f->x0,0,239);if(width<0) width=-width;
        int height=clamp(bottom,0,143)-clamp(top,0,143);
        if(height>0 && width*height>=16000) return 1;
    }
    return 0;
}
#ifdef DOK_RENDER_HW_DIRECT_PROTOTYPE
static DOK_COLD void background_gaps(dok_renderer_workspace *w,const dok_runtime_level *level) {
#else
static void background_gaps(dok_renderer_workspace *w,const dok_runtime_level *level) {
#endif
    (void)level;
    for(unsigned x=0;x<240;
#ifdef DOK_RENDER_HALF_COLUMNS
        x+=2
#else
        ++x
#endif
    ) {
        coverage_sync(w,x);
        unsigned begin=0,count=COVER_COUNT(w,x);
        for(unsigned k=0;k<=count;++k) {
            unsigned end=k<count?COVER(w,x,k)[0]:144;
            uint8_t *dst=w->frame+begin*240+x;
            for(;begin<end;++begin,dst+=240) store_pixel(dst,0);
            if(k<count) begin=(unsigned)COVER(w,x,k)[1]+1;
        }
    }
}
#endif
#if (defined(DOK_RENDER_BSP_PROTOTYPE) && !defined(DOK_RENDER_WORLD_PLANES_PROTOTYPE) && !defined(DOK_RENDER_HW_BSP_REUSE_PROTOTYPE)) || defined(DOK_RENDER_HW_FACE_CLASS_PROFILE_PROTOTYPE)
DOK_COLD
#endif
#ifdef DOK_RENDER_SECTOR_CLIP_PROTOTYPE
#include "renderer_sector.inc"
#endif
dok_renderer_status dok_renderer_draw(dok_renderer_workspace *w,const dok_runtime *r,const dok_rom_assets *a,
    unsigned level,unsigned language,const dok_projection_camera *c,dok_renderer_stats *stats) {
    dok_renderer_stats s;dok_renderer_status status;uint16_t id;dok_rom_resource light;unsigned i;
    if(!w || !r || !r->level || !a || !c || !stats) return DOK_RENDER_ARGUMENT;
    zero(&s,sizeof(s));s.face_capacity=DOK_RENDER_FACE_CAPACITY;
    if(level>=a->level_count || language>=a->language_count || !r->object_count || r->object_count>DOK_RUNTIME_MAX_OBJECTS ||
       r->object_count!=r->level->object_count || !r->level->definitions || !r->level->fields ||
       r->level->settings[DOK_LEVEL_WIDTH]!=64 || r->level->settings[DOK_LEVEL_LENGTH]!=64 || r->level->cells!=4096 ||
       r->level->settings[DOK_LEVEL_CELL_SIDE]!=480 || r->level->settings[DOK_LEVEL_CELL_HEIGHT]!=960 ||
       c->sine< -32768 || c->sine>32768 || c->cosine< -32768 || c->cosine>32768 || c->distance!=200 || c->scale!=8 || c->focal!=4096 || c->center_x!=159 || c->center_y!=79 ||
       c->x<0 || c->x>30720 || c->z<0 || c->z>30720 || c->y< -4096 || c->y>4096) {
        *stats=s;PROFILE_FAIL();
    }
    STAGE(0);w->face_count=0;w->surface_count=0;zero(w->image_known,sizeof(w->image_known));
#ifdef DOK_RENDER_HW_GEOMETRY_PROFILE_PROTOTYPE
    for(unsigned n=0;n<8;++n)g_geometry_work[n]=0;
#endif
#ifdef DOK_RENDER_HW_DIRECT_PROTOTYPE
    hw_tile_frame=w->frame;
#endif
#ifdef DOK_RENDER_STATIC_SETUP_PROTOTYPE
    static_view=level*2+language;
#endif
#ifdef DOK_RENDER_STREAM_BSP_PROTOTYPE
    stream_begin(w,r,c,level*2+language);
#endif
#ifdef DOK_RENDER_PRELIT_PROTOTYPE
    extern const uint32_t dok_prelit_offsets[];
#ifdef DOK_RENDER_EXACT_LIGHT_PROTOTYPE
    prelit_view=dok_prelit_offsets+(level*2+language)*(a->resource_count+1)*16;
#ifdef DOK_RENDER_HW_SPARSE_POSTS_PROTOTYPE
    sparse_view=dok_sparse_image_offsets+(level*2+language)*(a->resource_count+1)*16;
    hw_column_posts=0;
#endif
#else
    prelit_view=dok_prelit_offsets+(level*2+language)*(a->resource_count+1)*5;
#endif
#endif
    zero(w->surface_known,sizeof(w->surface_known));
#ifdef DOK_RENDER_COLUMN_PIPELINE_PROTOTYPE
    status=column_begin(w,r,a,level,language,c);if(status)goto done;
#endif
#ifdef DOK_RENDER_SECTOR_CLIP_PROTOTYPE
    status=sector_begin(w,r,a,level,language,c);if(status)goto done;
#endif
#ifdef DOK_RENDER_HW_STATIC_GEOMETRY_PROTOTYPE
    hw_static_begin(w,r,a,level*2+language,c);
#endif
#ifdef DOK_RENDER_WORLD_PLANES_PROTOTYPE
    world_ceiling_count=0;
    for(unsigned glyph=0;glyph<256;++glyph) world_planes[glyph].ready=0;
#ifdef DOK_RENDER_PROCEDURAL_PROTOTYPE
    extern const uint8_t dok_procedural_colors[];
    world_procedural_colors=dok_procedural_colors+(level*2+language)*256*6;
#endif
#endif
    if(dok_rom_binding(&id,a,level,language,DOK_ROM_GLOBAL_OBJECT,DOK_ROM_SLOT_LIGHT)!=DOK_ROM_OK ||
       dok_rom_resource_at(&light,a,id,language)!=DOK_ROM_OK || light.size!=4096) {status=DOK_RENDER_RESOURCE;goto done;}
#if defined(DOK_RENDERER_PROBE) && !defined(DOK_RENDER_WORLD_PLANES_PROTOTYPE)
    if(++surface_epoch==0x80000) {surface_epoch=1;zero(surface_vertices,sizeof(surface_vertices));}
#endif
    STAGE(1);
#ifdef DOK_RENDER_GEOMETRY_CACHE_PROTOTYPE
    if(++geometry_epoch==0x20000) {geometry_epoch=1;zero(geometry_vertices,sizeof(geometry_vertices));}
#endif
#ifdef DOK_RENDER_RAY_WALLS_PROTOTYPE
    status=ray_prepare(w,r,a,level,language);if(status) goto done;
#endif
#ifdef DOK_RENDER_BSP_PROTOTYPE
    status=bsp_prepare(w,r,a,level,language,c);if(status) goto done;
#ifdef DOK_RENDER_HW_BSP_REUSE_PROTOTYPE
    s.faces_built=w->face_count;
#endif
#endif
    status=build(w,r,a,level,language,c,&s);if(status) goto done;
#ifdef DOK_RENDER_HW_STATIC_GEOMETRY_PROTOTYPE
    hw_static_finish(w,r);
#endif
    if(w->sprite_count>DOK_RENDER_SPRITE_CAPACITY) {status=DOK_RENDER_CAPACITY;goto done;}
    for(i=0;i<w->sprite_count;++i) {
        const dok_renderer_sprite *sp=&w->sprites[i];
        if(sp->object>=r->object_count) {status=DOK_RENDER_ARGUMENT;goto done;}
        unsigned before=w->face_count;
        if(w->sprite_resource[i]) {
            dok_rom_resource res;dok_animation_view av;dok_animation_frame af;
            if(dok_rom_resource_at(&res,a,w->sprite_resource[i],language)!=DOK_ROM_OK ||
               dok_animation_open(res.data,res.size,&av)!=DOK_ASSET_OK ||
               dok_animation_frame_at(&av,w->sprite_frame[i],&af)!=DOK_ASSET_OK) {status=DOK_RENDER_RESOURCE;goto done;}
            dok_renderer_image *im=&w->instance_images[i];
            *im=(dok_renderer_image){af.image.pixels,0,0,af.image.width,af.image.height,8,w->sprite_resource[i]};
            status=billboard_image(w,r,c,&s,sp->object,(uint16_t)(256+i),im,sp->x,sp->y,sp->z);
        } else status=billboard(w,r,a,level,language,c,&s,sp->object,sp->x,sp->y,sp->z);
        if(status) goto done;
        (void)before;
    }
#ifdef DOK_RENDER_HW_LOW_ADAPTIVE_PROTOTYPE
    hw_low_choose(w,r);
#endif
    STAGE(2);
    sort_faces(w);
    STAGE(3);
#ifdef DOK_RENDERER_PROBE
#ifdef DOK_RENDER_HW_DIRECT_PROTOTYPE
    int deferred=g_hw_tile_active?0:defer_background(w);
#else
    int deferred=defer_background(w);
#endif
#ifdef DOK_RENDER_EAGER_CLEAR_PROTOTYPE
    deferred=0;
#endif
    clear_rows(w,r->level,deferred?144:0);
#else
    clear_rows(w,r->level,0);
#endif
#ifdef DOK_RENDER_STATIC_SETUP_PROTOTYPE
    if(lights_cache_view!=static_view+1) {
#endif
    for(i=0;i<4096;++i) w->lights[i]=light.data[i];
#ifdef DOK_RENDER_STATIC_SETUP_PROTOTYPE
    lights_cache_view=(uint8_t)(static_view+1);
    }
#endif
#ifdef DOK_RENDERER_PROBE
    zero(coverage_count,sizeof(coverage_count));
#ifdef DOK_RENDER_HALF_COLUMNS
    zero(coverage_bits,sizeof(coverage_bits));
#endif
#else
    zero(w->covered_count,sizeof(w->covered_count));
#endif
    unsigned walls=w->face_count,surfaces=w->surface_count;
#ifdef DOK_RENDER_HW_WORLD_OBJ_PROTOTYPE
    world_obj_begin(w,level*2+language,a->resource_count);
#endif
#ifdef DOK_RENDER_RAY_WALLS_PROTOTYPE
    ray_draw(w,r,c,&s);
#endif
#ifdef DOK_RENDERER_PROBE
    for(unsigned k=1;k<5;++k) g_render_work[k]=0;
#ifdef DOK_RENDER_HW_FACE_CLASS_PROFILE_PROTOTYPE
    for(unsigned k=0;k<4;++k)g_hw_face_class_work[k]=0;
#endif
#ifdef DOK_RENDER_FAST_OPAQUE_ARM_PROTOTYPE
    for(unsigned k=0;k<4;++k)g_opaque_work[k]=0;
#endif
#ifdef DOK_RENDER_RASTER_PROFILE_PROTOTYPE
    for(unsigned k=0;k<4;++k)g_raster_work[k]=0;
#endif
#ifdef DOK_RENDER_HW_BG_PROTOTYPE
    for(unsigned k=0;k<6;++k)g_hw_plane_work[k]=0;
#endif
#endif
#ifdef DOK_RENDER_STREAM_BSP_PROTOTYPE
    if(stream_active) {status=stream_draw(w,r,a,level,language,c,&s);if(status)goto done;goto stream_finished;}
#endif
#ifdef DOK_RENDER_COLUMN_PIPELINE_PROTOTYPE
    if(column_active) {status=column_draw(w,r,a,level,language,c,&s);if(status)goto done;goto column_finished;}
#endif
#ifdef DOK_RENDER_SECTOR_CLIP_PROTOTYPE
    if(sector_active){status=sector_draw(w,r,a,level,language,c,&s);if(status)goto done;goto sector_finished;}
#endif
    while(walls || surfaces) {
#ifdef DOK_RENDERER_PROBE
        uint32_t stamp=g_render_work[0]?render_ticks():0;
#endif
#ifdef DOK_RENDERER_PROBE
        unsigned index=walls?face_order[walls-1]:0;
#else
        unsigned index=walls?walls-1:0;
#endif
        const dok_renderer_surface *f=surfaces?&w->surfaces[w->surface_order[surfaces-1]]:0;
        if(f && (!walls || f->key<=w->faces[index].key)) {
            surface_raster(w,r,f,&s);--surfaces;
#ifdef DOK_RENDERER_PROBE
            if(g_render_work[0]) {g_render_work[2]+=render_ticks()-stamp;++g_render_work[4];}
#ifdef DOK_RENDER_HW_BG_PROTOTYPE
            if(g_render_work[0]) {g_hw_plane_work[f->plane]+=render_ticks()-stamp;++g_hw_plane_work[3+f->plane];}
#endif
#endif
        } else {
            raster(w,r,&w->faces[index],w->lights,&s);--walls;
#ifdef DOK_RENDER_HW_FACE_CLASS_PROFILE_PROTOTYPE
            if(g_render_work[0]) {
                unsigned group=(face_image(w,w->faces[index].image_index)->flags&8u)?0:1;
                g_hw_face_class_work[group]+=render_ticks()-stamp;++g_hw_face_class_work[2+group];
            }
#endif
#ifdef DOK_RENDERER_PROBE
            if(g_render_work[0]) {g_render_work[1]+=render_ticks()-stamp;++g_render_work[3];}
#endif
        }
    }
#ifdef DOK_RENDER_STREAM_BSP_PROTOTYPE
stream_finished:
#endif
#ifdef DOK_RENDER_COLUMN_PIPELINE_PROTOTYPE
column_finished:
#endif
#ifdef DOK_RENDER_SECTOR_CLIP_PROTOTYPE
sector_finished:
#endif
#ifdef DOK_RENDER_WORLD_PLANES_PROTOTYPE
#ifdef DOK_RENDER_RAY_WALLS_PROTOTYPE
    ray_publish(w);
#endif
    world_plane_draw(w,r,c,&s);
#endif
#ifdef DOK_RENDERER_PROBE
    if(deferred) background_gaps(w,r->level);
#endif
    STAGE(4);status=DOK_RENDER_OK;
done:
    s.face_count=w->face_count;*stats=s;return status;
}
