#ifndef DOK_NDS_COLLISION_CASES_H
#define DOK_NDS_COLLISION_CASES_H
#include "dok/collision.h"
typedef struct dok_collision_test_workspace {
    dok_level_definition definition;
    dok_initial_world world;
    dok_collision_state collision;
    dok_collision_context context;
} dok_collision_test_workspace;
typedef struct dok_collision_test_report {
    uint32_t checks, failures, first_failed_line, native_cases, transaction_cases, camera_cases;
} dok_collision_test_report;
int dok_collision_run_cases(dok_collision_test_workspace *workspace,
    dok_collision_test_report *report);
#endif
