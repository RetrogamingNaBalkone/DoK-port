#ifndef DOK_NDS_AI_H
#define DOK_NDS_AI_H
#include "dok/combat.h"
#define DOK_AI_LIMIT 128u
enum {DOK_AI_CALLBACK=0x1601};
enum {DOK_AI_WAKE=1,DOK_AI_CONTACT=2,DOK_AI_COURSE=3,DOK_AI_MOVED=4,
      DOK_AI_HURT=5,DOK_AI_ATTACK=6,DOK_AI_DYING=7,DOK_AI_DEAD=8,DOK_AI_RANGED=9,DOK_AI_MELEE=10};
typedef struct dok_ai_output {dok_object_ref object;uint32_t kind;int16_t state,health;} dok_ai_output;
typedef void (*dok_ai_sink_fn)(void *,const dok_ai_output *);
typedef struct dok_ai_services {void *context;dok_motion_trig_fn trig;dok_ai_sink_fn sink;} dok_ai_services;
typedef struct dok_enemy {
    dok_motion_state motion;
    uint32_t last_motion_ticks,burst_started;
    int16_t health,course_counter,next_cursor,displayed_cursor,shots;
    uint8_t glyph,state,displayed_kind,corpse,motion_started;
    uint8_t reserved;
} dok_enemy;
typedef struct dok_ai {
    dok_initial_world *world;
    dok_collision_state *collision;
    dok_event_scheduler *scheduler;
    const dok_level_presentation *presentation;
    dok_sprite_asset_bank *bank;
    dok_combat *combat;
    dok_ai_services services;
    dok_enemy enemies[DOK_AI_LIMIT];
    uint16_t glyph_index[256];
    unsigned char corpses[32];
    uint32_t epoch,count,active,callbacks,considerations,course_trials,motion_callbacks;
    uint32_t fault,revision,tag,busy,registered;
} dok_ai;
/* Stable per-world services, distinct from static object refs. No RNG is
 * present in these source branches. All parameters remain in RU definitions. */
dok_status dok_ai_bind(dok_ai *,dok_initial_world *,dok_collision_state *,dok_event_scheduler *,
    const dok_level_presentation *,dok_sprite_asset_bank *,dok_combat *,const dok_ai_services *);
dok_status dok_ai_register(dok_ai *);
dok_status dok_ai_consider(dok_ai *,dok_object_ref,int32_t hero_x,int32_t hero_z);
/* NDS deterministic consideration input: accepted N12 gather bounds/order,
 * scheduled in simulation time, independently of presentation cadence. */
dok_status dok_ai_consider_camera(dok_ai *,const dok_nds_render_camera *,uint32_t heading);
dok_status dok_ai_hit(dok_ai *,dok_object_ref target,dok_object_ref source,int16_t damage,uint32_t *accepted);
dok_status dok_ai_callback(dok_ai *,dok_object_ref,uint16_t callback);
dok_status dok_ai_clear(dok_ai *);
/* Source scalar boundaries; signed32 overflow/abs(INT_MIN) are refused. */
dok_status dok_ai_heading(int32_t dx,int32_t dz,int16_t *heading);
uint32_t dok_ai_near(int32_t dx,int32_t dz,int32_t activation,int16_t state);
uint32_t dok_ai_course_step(int16_t *counter,int16_t period);
uint32_t dok_ai_burst_due(uint32_t elapsed,int16_t delay);
int16_t dok_ai_damage_word(int16_t health,int16_t damage);
/* Controlled read-only source sight query: temporarily removes self MMap,
 * restores it before returning. No dispatch/render lease may overlap. */
dok_status dok_ai_sight(dok_ai *,dok_object_ref target,uint32_t *visible);
#endif
