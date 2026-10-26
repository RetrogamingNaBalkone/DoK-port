#ifndef DOK_COMBAT_H
#define DOK_COMBAT_H
#include "dok/items.h"
#include "dok/rom_assets.h"
#define DOK_COMBAT_SLOTS 256
typedef enum {DOK_COMBAT_SHOT=1,DOK_COMBAT_DAMAGE,DOK_COMBAT_IMPACT,DOK_COMBAT_MISS,
 DOK_COMBAT_REMOVED,DOK_COMBAT_EMPTY,DOK_COMBAT_FULL,DOK_COMBAT_WEAPON,DOK_COMBAT_DEATH,DOK_COMBAT_WAKE,DOK_COMBAT_RECLAIM} dok_combat_kind;
typedef void (*dok_combat_sink)(void *,dok_combat_kind,uint16_t slot,uint16_t target,int16_t value);
/* One native projectile callback per owner. Ring links and deadline specialize
 * scheduler.c's single-event case, including its mutable-count removal skip. */
typedef struct {
 int32_t x,z;uint32_t deadline,last;
 int16_t y,heading,damage,floor;
 uint16_t object,owner,resource,frame,next,previous;
 uint8_t state,glyph;uint16_t reserved;
} dok_combat_projectile;
typedef struct {
 dok_runtime *world;const dok_rom_assets *assets;dok_items *items;
 dok_combat_projectile slots[DOK_COMBAT_SLOTS];
 uint8_t occupied[512]; /* MMap dynamic occupancy; no static-glyph aliasing */
 uint32_t now,weapon_deadline,shots,removed,hits,misses,errors;
 uint16_t cursor,active,weapon_state,weapon_shift,weapon_frame,weapon_resource;
 uint16_t current,dispatching,pending_first,pending_last;
 uint16_t displayed_weapon;
 unsigned level,language;dok_combat_sink sink;void *context;
} dok_combat;
void dok_combat_init(dok_combat *,dok_runtime *,const dok_rom_assets *,dok_items *,unsigned level,unsigned language,uint32_t tick,dok_combat_sink,void *);
int dok_combat_request(dok_combat *,int16_t slot);
int dok_combat_fire(dok_combat *);
/* G15 may spawn from any actor with an owner object ID; same-class immunity. */
int dok_combat_spawn(dok_combat *,uint16_t owner,uint8_t template_glyph);
int dok_combat_damage(dok_combat *,uint16_t target,uint16_t source,int16_t damage);
void dok_combat_poll(dok_combat *,uint32_t tick);
void dok_combat_clear(dok_combat *);
void dok_combat_remove(dok_combat *,uint16_t slot);
/* Occupied dynamic MMap entry: immutable template object ID (class/flags),
 * not a dynamic slot ID. NONE when no projectile owns the cell. */
uint16_t dok_combat_object_at(const dok_combat *,int32_t,int32_t);
int dok_combat_blocked(void *,int32_t x,int32_t z);
#endif
