#ifndef DOK_EFFECTS_RUNTIME_H
#define DOK_EFFECTS_RUNTIME_H
#include "dok/effect_cache.h"
#include "dok/sprite_assets.h"
#include "dok/render_materials.h"
#include "dok/time.h"
#ifndef EFFECT_REAL_CLOCK
#define EFFECT_REAL_CLOCK 0
#endif
typedef struct NdsEffectTrace {
    uint32_t magic,sequence,ordinal,epoch,case_id,frame_id,source_ticks,kind,glyph;
    uint32_t counter,displayed,interval,cancel;
} NdsEffectTrace;
#define NDS_EFFECT_TRACE_CAPACITY 512u
typedef struct NdsEffectsRuntime {
    unsigned char light[4096],rgb6[768];
    uint16_t palette[4096];
    dok_effect_cache cache;
    dok_water_state water[256];
    dok_sprite_player players[256];
    uint16_t material_flags[DOK_RENDER_MATERIALS],material_phase[DOK_RENDER_MATERIALS];
    unsigned char material_water[DOK_RENDER_MATERIALS];
    dok_world_clock clock;
    dok_event_scheduler scheduler;
    dok_render_world *world;
    dok_sprite_asset_bank *sprites;
    uint32_t fixture_ticks,callbacks,water_callbacks,animation_callbacks,fault,percentage,schedule_refusals;
    uint32_t case_id,frame_id,last_ticks;
    volatile NdsEffectTrace *traces;
    struct NdsGameplay *game;
    dok_sprite_list *simulation_list;
    uint32_t trace_total; /* Initialized once by the shell; preserved across level loads. */
} NdsEffectsRuntime;
dok_status nds_effects_load(NdsEffectsRuntime *runtime,dok_render_world *world,
    dok_sprite_asset_bank *sprites,const dok_asset_session *session,const dok_asset_reader *reader,
    dok_event_bank *events,const dok_platform_services *platform,unsigned char *pool,uint32_t capacity);
dok_status nds_effects_begin(NdsEffectsRuntime *runtime);
dok_status nds_effects_arm(NdsEffectsRuntime *runtime,const dok_render_scene *scene,const dok_sprite_list *sprites);
dok_status nds_effects_arm_bounds(NdsEffectsRuntime *,const dok_render_gather_bounds *,const dok_sprite_list *);
#endif
