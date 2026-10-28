/* Camera-stable static geometry cache. DIRECT_GAPS never uses the legacy
 * interval payload during hardware draws, so it can hold this persistent data.
 * Dynamic cell billboards, center panels and instances remain in normal order. */
#if !defined(DOK_RENDER_HW_DIRECT_GAPS_PROTOTYPE) || !defined(DOK_RENDER_NO_SCALES_PROTOTYPE) || defined(DOK_RENDER_BSP_PROTOTYPE)
#error Static geometry cache requires direct hardware coverage and no BSP scratch
#endif
extern void dok_gba_geometry_copy(void *,const void *,unsigned);
extern unsigned dok_gba_geometry_equal(const void *,const void *,unsigned);
#ifdef DOK_RENDER_HW_WORLD_OBJ_PROTOTYPE
#include "dok/gba_world_obj.h"
#define HW_STATIC_PAYLOAD_LIMIT (sizeof(w->covered)-7-sizeof(*hw_static)-DOK_WORLD_OBJ_SCRATCH)
#elif defined(DOK_RENDER_HW_BG_LOWERED_PROTOTYPE)
#include "dok/gba_hw_scratch.h"
#define HW_STATIC_PAYLOAD_LIMIT (sizeof(w->covered)-7-sizeof(*hw_static)-DOK_HW_LOW_FRAME_BYTES-DOK_HW_OAM_RESERVED_BYTES)
#elif defined(DOK_RENDER_HW_OAM_RAM_PROTOTYPE)
#include "dok/gba_hw_scratch.h"
#define HW_STATIC_PAYLOAD_LIMIT (sizeof(w->covered)-7-sizeof(*hw_static)-DOK_HW_OAM_RESERVED_BYTES)
#else
#define HW_STATIC_PAYLOAD_LIMIT (sizeof(w->covered)-3-sizeof(*hw_static))
#endif
typedef struct {
    unsigned face_bytes,surface_count,cells,wall_count;
    unsigned wall_cells,faces_built,faces_rejected,near_clipped;
    uint8_t face_counts[512],wall_ids[256],image_known[256],surface_known[256][3];
    uint32_t tmap[1024];
    uint8_t payload[];
} hw_static_cache;
_Static_assert((sizeof(hw_static_cache)&3)==0,"geometry payload alignment");
_Static_assert((offsetof(hw_static_cache,image_known)&3)==0 && (offsetof(hw_static_cache,surface_known)&3)==0,"geometry leases alignment");
static hw_static_cache *hw_static;
static dok_renderer_workspace *hw_static_owner;
static const dok_runtime_level *hw_static_level;
static const dok_rom_assets *hw_static_assets;
static dok_projection_camera hw_static_camera;
static unsigned hw_static_view,hw_static_active,hw_static_valid,hw_static_mode,hw_static_stable;
static unsigned hw_static_cursor,hw_static_first,hw_static_rejected,hw_static_clipped,hw_static_walls;
volatile uint32_t g_hw_static_work[4]; /* reuse frames,capture frames,miss frames,capacity fallbacks */
enum {HW_STATIC_OFF,HW_STATIC_CAPTURE,HW_STATIC_REUSE};
static DOK_COLD int hw_static_walls_valid(const dok_runtime *r) {
    for(unsigned k=0;k<hw_static->wall_count;++k) {
        const dok_runtime_object *o=&r->objects[hw_static->wall_ids[k]];
        if((o->flags&0x800) || o->visual_resource)return 0;
    }
    return 1;
}
static DOK_COLD void hw_static_begin(dok_renderer_workspace *w,const dok_runtime *r,const dok_rom_assets *a,unsigned view,const dok_projection_camera *c) {
    hw_static_mode=HW_STATIC_OFF;
    if(!g_hw_tile_active) {hw_static_valid=hw_static_active=0;return;}
    int same=hw_static_active && hw_static_owner==w && hw_static_level==r->level && hw_static_assets==a && hw_static_view==view &&
        dok_gba_geometry_equal(c,&hw_static_camera,sizeof(*c));
    hw_static_active=1;
    if(!same) {
        hw_static_valid=hw_static_stable=0;hw_static_owner=w;hw_static_level=r->level;hw_static_assets=a;hw_static_view=view;
        dok_gba_geometry_copy(&hw_static_camera,c,sizeof(*c));++g_hw_static_work[2];return;
    }
    hw_static=(hw_static_cache *)(((uintptr_t)w->covered+3)&~(uintptr_t)3);
    if(hw_static_valid && hw_static_walls_valid(r) && dok_gba_geometry_equal(hw_static->tmap,r->maps+4096,4096)) {
        hw_static_mode=HW_STATIC_REUSE;hw_static_cursor=0;++g_hw_static_work[0];return;
    }
    /* A turn can contain an occasional draw with no simulation step. Avoid
     * paying capture cost for those isolated repeats of a moving camera. */
    if(hw_static_stable<3) {++hw_static_stable;return;}
    hw_static_valid=0;hw_static->wall_count=0;
    for(unsigned id=0;id<r->object_count;++id)if(r->level->definitions[id].class_id==2)
        hw_static->wall_ids[hw_static->wall_count++]=(uint8_t)id;
    if(!hw_static_walls_valid(r)) {++g_hw_static_work[2];return;}
    hw_static->face_bytes=hw_static->surface_count=0;
    hw_static->wall_cells=hw_static->faces_built=hw_static->faces_rejected=hw_static->near_clipped=0;
    zero(hw_static->face_counts,sizeof(hw_static->face_counts));
    hw_static_mode=HW_STATIC_CAPTURE;++g_hw_static_work[1];
}
static DOK_COLD void hw_static_envelope(dok_renderer_workspace *w,unsigned cells,dok_renderer_stats *s) {
    if(hw_static_mode==HW_STATIC_OFF)return;
    if(cells>512) {hw_static_mode=hw_static_valid=0;++g_hw_static_work[3];return;}
    if(hw_static_mode==HW_STATIC_CAPTURE) {hw_static->cells=cells;return;}
    if(hw_static->cells!=cells) {hw_static_mode=hw_static_valid=0;return;}
    unsigned count=hw_static->surface_count;
    dok_gba_geometry_copy(w->surfaces,hw_static->payload+hw_static->face_bytes,count*sizeof(w->surfaces[0]));
    dok_gba_geometry_copy(w->surface_order,hw_static->payload+hw_static->face_bytes+count*sizeof(w->surfaces[0]),(count*2+3)&~3u);
    w->surface_count=(uint16_t)count;
    dok_gba_geometry_copy(w->image_known,hw_static->image_known,sizeof(w->image_known));
    dok_gba_geometry_copy(w->surface_known,hw_static->surface_known,sizeof(w->surface_known));
    s->wall_cells+=hw_static->wall_cells;s->faces_built+=hw_static->faces_built;
    s->faces_rejected+=hw_static->faces_rejected;s->near_clipped+=hw_static->near_clipped;
}
static DOK_COLD dok_renderer_status hw_static_restore_cell(dok_renderer_workspace *w,unsigned cell) {
    unsigned n=hw_static->face_counts[cell],bytes=n*sizeof(w->faces[0]);
    if(w->face_count+n>DOK_RENDER_FACE_CAPACITY)return DOK_RENDER_CAPACITY;
    dok_gba_geometry_copy(w->faces+w->face_count,hw_static->payload+hw_static_cursor,bytes);
    w->face_count=(uint16_t)(w->face_count+n);hw_static_cursor+=bytes;
    return DOK_RENDER_OK;
}
static DOK_COLD void hw_static_wall_begin(const dok_renderer_workspace *w,const dok_renderer_stats *s) {
    hw_static_first=w->face_count;hw_static_rejected=s->faces_rejected;hw_static_clipped=s->near_clipped;hw_static_walls=s->wall_cells;
}
static DOK_COLD void hw_static_wall_end(dok_renderer_workspace *w,unsigned cell,const dok_renderer_stats *s) {
    unsigned n=w->face_count-hw_static_first,bytes=n*sizeof(w->faces[0]);
    if(n>255 || hw_static->face_bytes+bytes>HW_STATIC_PAYLOAD_LIMIT) {
        hw_static_mode=hw_static_valid=0;++g_hw_static_work[3];return;
    }
    hw_static->face_counts[cell]=(uint8_t)n;
    dok_gba_geometry_copy(hw_static->payload+hw_static->face_bytes,w->faces+hw_static_first,bytes);
    hw_static->face_bytes+=bytes;hw_static->faces_built+=n;
    hw_static->faces_rejected+=s->faces_rejected-hw_static_rejected;hw_static->near_clipped+=s->near_clipped-hw_static_clipped;
    hw_static->wall_cells+=s->wall_cells-hw_static_walls;
}
static DOK_COLD void hw_static_finish(dok_renderer_workspace *w,const dok_runtime *r) {
    if(hw_static_mode!=HW_STATIC_CAPTURE)return;
    unsigned count=w->surface_count,bytes=count*sizeof(w->surfaces[0]),order=(count*2+3)&~3u;
    if(hw_static->face_bytes+bytes+order>HW_STATIC_PAYLOAD_LIMIT) {
        hw_static_mode=hw_static_valid=0;++g_hw_static_work[3];return;
    }
    hw_static->surface_count=count;
    dok_gba_geometry_copy(hw_static->payload+hw_static->face_bytes,w->surfaces,bytes);
    dok_gba_geometry_copy(hw_static->payload+hw_static->face_bytes+bytes,w->surface_order,order);
    dok_gba_geometry_copy(hw_static->tmap,r->maps+4096,4096);
    zero(hw_static->image_known,sizeof(hw_static->image_known));
    for(unsigned k=0;k<hw_static->wall_count;++k) {
        unsigned id=hw_static->wall_ids[k];hw_static->image_known[id]=w->image_known[id];
    }
    dok_gba_geometry_copy(hw_static->surface_known,w->surface_known,sizeof(w->surface_known));
    hw_static_valid=1;
}
