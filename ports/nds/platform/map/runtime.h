#ifndef NDS_MAP_RUNTIME_H
#define NDS_MAP_RUNTIME_H
#include "dok/map_control.h"
#include "dok/map_raster.h"
#include "../gameplay_probe/gameplay.h"
#include "dok/render_materials.h"
#include "dok/time.h"
typedef struct NdsMapRuntime {
    dok_map_control control;
    dok_map_snapshot snapshot;
    dok_map_glyph glyphs[256];
    unsigned char visited_snapshot[DOK_LEVEL_MAX_CELLS];
    unsigned char *planes,*post_visited;
    uint16_t palette[256];
    uint32_t paused,start_held,absorb,pauses,resumes;
    uint32_t updates,upload_bytes,verified,fault,input_ticks,latency_max,input_pending;
    uint32_t touch_held,touch_x,touch_y,level,case_id,ordinal,frame,serial;
    uint32_t discovery_records,discovery_marked;
    uint32_t hero_visited;
    int32_t left,top;
    /* N21P diagnostic timings, cpuGetTiming units; no heap/global slab. */
    uint32_t profile[64];
    /* N21P bounded diagnostic history. Lives in existing scene tail, never
     * the heap/reserve; each 48-u32 record owns its publication seqlock. */
    uint32_t profile_history[8][48];
    /* Cached descriptor is the last displayed map, not the current world.
     * live[] publishes current input/simulation scalars under the same lock. */
    uint32_t live[48];
    uint32_t force,language,last_draw_ticks,last_draw_frame,last_draw_reason;
    uint32_t skips,deferred;
    uint32_t class1_count;
    uint8_t class1_glyphs[4];
    uint32_t pending_probe[64];
    uint32_t prepared,pending_reason,prepare_ticks,pending_ticks;
    uint32_t pending_upload_bytes,button_valid,button_capture,button_language;
    const unsigned char *button_pixels;
    unsigned char button_font[64];
} NdsMapRuntime;
#ifdef N21P_HOST_TEST
int nds_map_test_bytes_differ(const void *,const void *,uint32_t);
#endif
extern volatile uint32_t g_nds_map_probe[64];
void nds_map_begin(void);
void nds_map_load(NdsMapRuntime *,dok_render_world *,NdsGameplay *,uint32_t level);
/* Frontend screens own SUB VRAM. Reassert bitmap/palette externally, then
 * invalidate even if the world and cached INDEX8 bytes have not changed. */
void nds_map_invalidate(NdsMapRuntime *);
/* Return gameplay keys after modal pause/release absorption. Touch is never
 * passed into the gameplay command word. Call outside scheduler dispatch. */
uint32_t nds_map_input(NdsMapRuntime *,dok_world_clock *,uint32_t keys,
                      uint32_t touch,int16_t x,int16_t y,NdsGameplay *);
void nds_map_update(NdsMapRuntime *,dok_render_world *,NdsGameplay *,
                    const unsigned char *font,uint32_t language,
                    unsigned char *pixels,void *vram,
                    uint32_t ticks,uint32_t level,uint32_t serial,
                    uint32_t frame,uint32_t ordinal,uint32_t case_id,
                    const dok_nds_render_camera *,uint32_t heading);
/* Prepare CPU bitmap/snapshot work before waiting for VBlank; commit performs
 * the actual scanout DMA and closes the publication lock after that wait. */
void nds_map_prepare(NdsMapRuntime *,dok_render_world *,NdsGameplay *,
                    const unsigned char *font,uint32_t language,
                    unsigned char *pixels,
                    uint32_t ticks,uint32_t level,uint32_t serial,
                    uint32_t frame,uint32_t ordinal,uint32_t case_id,
                    const dok_nds_render_camera *,uint32_t heading);
void nds_map_commit(NdsMapRuntime *,unsigned char *pixels,void *vram);
/* Bounded DMA only; keep snapshot odd until later commit verifies/publishes.
 * Repeated transfer is inert. Commit calls transfer if the caller omitted it. */
void nds_map_transfer(NdsMapRuntime *,unsigned char *pixels,void *vram);
#endif
