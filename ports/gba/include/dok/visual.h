#ifndef DOK_VISUAL_H
#define DOK_VISUAL_H
#include "dok/rom_assets.h"
#include "dok/scheduler.h"
typedef void (*dok_visual_sink)(void *,uint16_t object,uint16_t frame,int complete);
/* Visual completion never changes health, AI state, collision or ownership. */
typedef struct {
 dok_scheduler scheduler;dok_event_owner owners[DOK_RUNTIME_MAX_OBJECTS];
 dok_runtime *world;const dok_rom_assets *assets;
 uint16_t resource[DOK_RUNTIME_MAX_OBJECTS];
 uint8_t loop[DOK_RUNTIME_MAX_OBJECTS];
 unsigned level,language;uint32_t now,steps,completed,errors,next,phase;
 dok_visual_sink sink;void *context;
} dok_visual;
void dok_visual_init(dok_visual *,dok_runtime *,const dok_rom_assets *,unsigned level,unsigned language,uint32_t tick,dok_visual_sink,void *);
int dok_visual_play(dok_visual *,uint16_t object,uint16_t slot,int loop);
void dok_visual_poll(dok_visual *,uint32_t tick);
/* Production 30Hz simulation timeline. Up to128 quanta per call, debt retained.
 * Raw poll above preserves native single-sweep semantics for controlled probes. */
void dok_visual_advance(dok_visual *,uint32_t tick,const uint8_t visible[DOK_RUNTIME_MAX_OBJECTS]);
/* Equivalent to the native base image getter's 200-step lease. Call for
 * considered images, independently of the display refresh rate. */
void dok_visual_touch(dok_visual *,uint16_t object);
#endif
