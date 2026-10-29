#ifndef DOK_AI_CASES_H
#define DOK_AI_CASES_H
#include "combat_cases.h"
#include "dok/ai.h"
typedef struct dok_ai_cases_workspace {
    dok_combat_cases_workspace game;
    dok_ai ai;
    dok_initial_world initial;
    dok_motion_state hero;
    uint32_t (*profile_counter)(void);
    uint32_t callback_ticks,max_callback_ticks,profile_samples;
    uint32_t stress_ticks,max_stress_ticks,stress_samples,profile_stress;
} dok_ai_cases_workspace;
typedef struct dok_ai_cases_report {uint32_t checks,failures,line,scenarios,callbacks,trace_hash,considerations,motion_callbacks,course_trials,stress_callbacks;} dok_ai_cases_report;
int dok_ai_cases(dok_ai_cases_workspace *,dok_ai_cases_report *,const char *);
#endif
