#ifndef DOK_INTERACTIONS_H
#define DOK_INTERACTIONS_H
#include "dok/runtime.h"
#include "dok/scheduler.h"

/* Explicit reference presentation cadence. DOS derives this from measured FPS;
 * the port advances 30 visual steps per 10000 native ticks independently of draw.
 * Hero/zone sampling remains the G07 416-tick fixed update. */
#define DOK_INTERACTION_FPS 30
enum { DOK_INTERACTION_USE=0x800 };
typedef enum {
 DOK_INTERACTION_LOCKED=1, DOK_INTERACTION_OPENED, DOK_INTERACTION_CLOSED,
 DOK_INTERACTION_STAT, DOK_INTERACTION_FINISH, DOK_INTERACTION_UNSUPPORTED
} dok_interaction_kind;
typedef void (*dok_interaction_sink)(void *,dok_interaction_kind,uint16_t,int32_t,int32_t);
typedef struct {
 dok_runtime *world;
 dok_scheduler scheduler;
 dok_event_owner owners[DOK_RUNTIME_MAX_OBJECTS];
 uint32_t now,visual_steps;
 uint16_t previous_zone,errors;
 dok_interaction_sink sink;
 void *context;
} dok_interactions;
void dok_interactions_init(dok_interactions *,dok_runtime *,uint32_t,
                           dok_interaction_sink,void *);
/* One scheduler sweep at supplied time; no implied catch-up. */
void dok_interactions_poll(dok_interactions *,uint32_t);
/* Event bits are consumed exactly as native handlers do. Source is object ID,
 * zero-based. Sensors bypass locks, players use world.player.keys. */
uint32_t dok_interactions_event(dok_interactions *,uint16_t,uint32_t,uint16_t);
void dok_interactions_use(dok_interactions *,const dok_runtime_actor *);
/* Sample FCMap before movement, as original hero callback does. */
void dok_interactions_zone(dok_interactions *,dok_runtime_actor *);
#endif
