#ifndef DOK_AI_H
#define DOK_AI_H
#include "dok/collision.h"
#include "dok/scheduler.h"
#include "dok/rom_assets.h"
#include "dok/combat.h"
/* Native image-query perception and active event motion. No RNG is used. */
typedef enum {DOK_AI_WAKE=1,DOK_AI_COURSE,DOK_AI_BLOCKED,DOK_AI_CONTACT,DOK_AI_ATTACK,DOK_AI_HURT,DOK_AI_DYING,DOK_AI_DEAD,DOK_AI_MELEE,DOK_AI_RANGED} dok_ai_kind;
typedef void (*dok_ai_sink)(void *,dok_ai_kind,uint16_t);
typedef struct {
 dok_runtime *world; dok_combat *combat; const dok_rom_assets *assets;
 dok_scheduler scheduler; dok_event_owner owners[DOK_RUNTIME_MAX_ACTORS];
 dok_motion motion[DOK_RUNTIME_MAX_ACTORS];
 uint32_t last[DOK_RUNTIME_MAX_ACTORS],now,steps,wakes,courses,blocked,contacts,errors;
 uint32_t attacks,hurts,deaths,completed,melee,ranged;
 unsigned level,language; dok_ai_sink sink; void *context;
} dok_ai;
int16_t dok_ai_heading(int32_t dx,int32_t dz);
int dok_ai_near(int32_t dx,int32_t dz,int32_t distance);
void dok_ai_init(dok_ai *,dok_runtime *,const dok_rom_assets *,unsigned,unsigned,uint32_t,dok_ai_sink,void *);
int dok_ai_wake(dok_ai *,uint16_t);
void dok_ai_consider(dok_ai *,uint16_t);
void dok_ai_remove(dok_ai *,uint16_t);
void dok_ai_connect_combat(dok_ai *,dok_combat *);
void dok_ai_notice(dok_ai *,unsigned,uint16_t);
void dok_ai_poll(dok_ai *,uint32_t);
#endif
