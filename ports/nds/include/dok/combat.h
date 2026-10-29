#ifndef DOK_NDS_COMBAT_H
#define DOK_NDS_COMBAT_H
#include "dok/items.h"
#include "dok/sprite_assets.h"
#include "dok/events.h"
#include "dok/controls.h"
enum { DOK_COMBAT_PROJECTILE=0x1501,DOK_COMBAT_WEAPON=0x1502,DOK_COMBAT_SWITCH=0x1503 };
enum { DOK_COMBAT_SHOT=1,DOK_COMBAT_IMPACT=2,DOK_COMBAT_MISS=3,DOK_COMBAT_EMPTY=4,
       DOK_COMBAT_RECLAIM=5,DOK_COMBAT_REMOVED=6,DOK_COMBAT_DAMAGE=7,DOK_COMBAT_HERO_DEAD=8 };
typedef struct dok_combat_output {
    dok_object_ref object,source,target;
    uint32_t kind;
    int16_t damage;
    uint16_t template_glyph;
} dok_combat_output;
typedef void (*dok_combat_sink_fn)(void *,const dok_combat_output *);
/* Only class9 is delegated; sleeping/hurt/death belong to N16/N17.
 * Called at an exclusive simulation boundary. accepted0 passes the projectile. */
typedef dok_status (*dok_combat_hit_fn)(void *,dok_object_ref target,
    dok_object_ref source,int16_t damage,uint32_t *accepted);
typedef struct dok_combat_services {
    void *context;
    dok_combat_sink_fn sink;
    dok_combat_hit_fn hit;
    dok_motion_trig_fn trig;
} dok_combat_services;
typedef struct dok_projectile {
    dok_object_ref owner;
    int32_t x,y,z;
    uint32_t generation,last_ticks;
    int16_t heading,speed,damage,damage_floor,cursor;
    uint16_t cell;
    uint8_t template_glyph,phase,occupied,effect_hit;
} dok_projectile;
typedef struct dok_weapon_runtime { int16_t state,shift,cursor;uint16_t active; } dok_weapon_runtime;
typedef struct dok_combat {
    dok_items *items;
    dok_event_scheduler *scheduler;
    const dok_level_presentation *presentation;
    const dok_sprite_asset_bank *bank;
    dok_combat_services services;
    dok_projectile projectiles[256];
    dok_weapon_runtime weapons[DOK_WEAPON_SLOTS];
    unsigned char occupied[512];
    uint32_t epoch,revision,active,high_water,callbacks,fault,tag,busy,registered;
    uint32_t executing,swap_active;
} dok_combat;
dok_status dok_combat_bind(dok_combat *,dok_items *,dok_event_scheduler *,
    const dok_level_presentation *,const dok_sprite_asset_bank *,const dok_combat_services *);
dok_status dok_combat_register(dok_combat *);
/* Ordered N09 input service, outside dispatch/collision/render lease. */
dok_status dok_combat_controls(dok_combat *,const dok_control_commands *);
dok_status dok_combat_spawn(dok_combat *,dok_object_ref owner,uint8_t template_glyph,
    int32_t x,int32_t z,int16_t heading,dok_object_ref *instance);
dok_status dok_combat_callback(dok_combat *,dok_object_ref service_owner,
    uint16_t callback,uint32_t parameter);
/* Stops instance events and unbinds overlay; outside dispatch, before borrower
 * destruction/world reset. Owner removal explicitly clears its projectiles. */
dok_status dok_combat_remove_owner(dok_combat *,dok_object_ref);
dok_status dok_combat_clear(dok_combat *);
dok_status dok_combat_hurt_hero(dok_combat *,dok_object_ref source,int16_t damage,uint32_t *accepted);
dok_status dok_combat_append_sprites(const dok_combat *,const dok_nds_render_camera *,dok_sprite_list *);
/* Actual source-facing scalar boundaries used by native oracles. */
int16_t dok_projectile_decay(int16_t damage,int16_t floor);
dok_status dok_projectile_distance(int16_t speed,uint32_t elapsed,int16_t side,
    int32_t *distance,int16_t *quarter);
dok_status dok_weapon_step(dok_weapon_runtime *,uint32_t frame_count,uint32_t *cancel);
uint32_t dok_weapon_ready(dok_weapon_runtime *);
typedef struct dok_weapon_view {dok_sprite_frame frame;int32_t left,top;uint32_t visible;} dok_weapon_view;
/* Source320x160 screen-space image position, independently from world camera. */
dok_status dok_combat_weapon_view(const dok_combat *,dok_weapon_view *);
/* Source aspect correction0.8/0.96, index0 transparency. Clears only scene
 * rows0..153, preserves HUD rows154..191. Source POT pixels are immutable. */
dok_status dok_weapon_overlay(const dok_weapon_view *,const unsigned char *pixels,
    uint16_t storage_width,uint32_t source_bytes,unsigned char *framebuffer,uint32_t bytes);
#endif
