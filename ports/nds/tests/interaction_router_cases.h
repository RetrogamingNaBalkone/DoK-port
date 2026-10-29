#ifndef DOK_NDS_INTERACTION_ROUTER_CASES_H
#define DOK_NDS_INTERACTION_ROUTER_CASES_H
#include "dok/interactions.h"
#include "dok/controls.h"
typedef struct dok_interaction_router_workspace {
    dok_level_definition definition;
    dok_initial_world world;
    dok_collision_state collision;
    dok_event_scheduler scheduler;
    dok_event_bank bank;
    dok_interactions_state interactions;
    dok_input_buffer input;
    dok_control_latch actions, hero;
    dok_motion_state motion;
    uint64_t sequence, counter;
    uint32_t raw_held, now, in_callback, callback_errors;
    uint32_t main_uses, use_inside_callback, hero_calls, door_calls;
    uint32_t hero_forward[8], hero_fire[8], hero_action_bits[8];
} dok_interaction_router_workspace;
typedef struct dok_interaction_router_report {
    uint32_t checks, failures, first_failed_line, pulse_cases, use_cases, pause_cases;
} dok_interaction_router_report;
_Static_assert(sizeof(dok_interaction_router_workspace)<=DOK_NDS_WORLD_BYTES,
    "N11 router suite fits startup world union");
int dok_interaction_router_run_cases(dok_interaction_router_workspace *workspace,
    dok_interaction_router_report *report);
#endif
