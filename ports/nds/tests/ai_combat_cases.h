#ifndef DOK_AI_COMBAT_CASES_H
#define DOK_AI_COMBAT_CASES_H
#include "ai_cases.h"
/* Scalar transcript, no pointers/padding; host oracle observes original state
 * branches independently. Each record is an actual C callback, not prediction. */
typedef struct dok_ai_combat_trace {
    int32_t language,level,glyph,scenario,state_in,health_in,next_in,kind_in,cursor_in,
        course_in,shots_in,elapsed,front_class,visible,flags_in,height,
        state_out,health_out,next_out,kind_out,cursor_out,course_out,shots_out,
        flags_out,corpse_out,interval,melee,ranged,dead,plane,hero_in,hero_out,cadence;
} dok_ai_combat_trace;
typedef void (*dok_ai_trace_fn)(void *,const dok_ai_combat_trace *);
typedef struct dok_ai_combat_cases_workspace {
    dok_ai_cases_workspace simulation;
    dok_ai_trace_fn trace;
    void *trace_context;
    uint32_t notices[11],combat_notices[9],language,level,scenario,front_class,visible,trace_hash,trace_count;
    uint32_t cadence,render_next,observers,functional_hash[2][2];
} dok_ai_combat_cases_workspace;
typedef struct dok_ai_combat_cases_report {
    uint32_t checks,failures,line,scenarios,callbacks,trace_hash,groups,placed_definitions,
        deaths,melee,ranged,trace_count,negative_intervals;
} dok_ai_combat_cases_report;
int dok_ai_combat_cases(dok_ai_combat_cases_workspace *,dok_ai_combat_cases_report *,const char *);
#endif
