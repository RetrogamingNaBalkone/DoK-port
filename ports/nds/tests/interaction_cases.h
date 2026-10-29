#ifndef DOK_NDS_INTERACTION_CASES_H
#define DOK_NDS_INTERACTION_CASES_H
#include "dok/interactions_move.h"
typedef struct dok_interaction_test_workspace {
    dok_level_definition definition;
    dok_initial_world world;
    dok_collision_state collision;
    dok_event_scheduler scheduler;
    dok_event_bank bank;
    dok_interactions_state interactions;
    dok_interaction_phase_view phases;
    dok_interaction_output outputs[32];
    uint32_t now, output_count, callback_errors, reentry_checks, callbacks;
} dok_interaction_test_workspace;
typedef struct dok_interaction_test_report {
    uint32_t checks, failures, first_failed_line;
    uint32_t door_cases, floor_cases, guard_cases;
} dok_interaction_test_report;
_Static_assert(sizeof(dok_interaction_test_workspace)<=DOK_NDS_WORLD_BYTES,
    "N11 unit workspace fits startup world union (not live scratch)");
int dok_interaction_run_cases(dok_interaction_test_workspace *workspace,
    dok_interaction_test_report *report);
#endif
