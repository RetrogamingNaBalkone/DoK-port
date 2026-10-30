#ifndef NDS_GAMEPLAY_H
#define NDS_GAMEPLAY_H
#include "dok/ai.h"
#include "dok/interactions_move.h"
typedef struct NdsGameplay {
#if GAMEPLAY_UI
    struct NdsUiRuntime *ui;
#endif
    dok_ai ai;
    dok_combat combat;
    dok_items items;
    dok_collision_state collision;
    dok_interactions_state interactions;
    dok_motion_state hero;
    dok_control_latch controls;
    uint32_t last_hero,started,hero_callbacks,fault,outputs[16],ai_outputs[16];
    uint32_t c2,input_sequence,input_held,case_kind,case_started;
    uint32_t callback_ticks,ai_ticks,combat_ticks,hero_ticks,max_service_ticks;
    int16_t pending_category[4],pending_value[4];
    uint32_t pending_count;
    uint32_t input_tick,input_pending,input_latency_max;
} NdsGameplay;
struct NdsEffectsRuntime;
dok_status nds_gameplay_bind(NdsGameplay *,struct NdsEffectsRuntime *);
dok_status nds_gameplay_callback(NdsGameplay *,struct NdsEffectsRuntime *,dok_object_ref,uint16_t,uint32_t);
dok_status nds_gameplay_input(NdsGameplay *,uint32_t held);
dok_status nds_gameplay_pose(NdsGameplay *,int32_t,int32_t,int32_t,int16_t);
#endif
