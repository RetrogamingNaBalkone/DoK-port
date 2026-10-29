#include "movement_replay.h"
#include <stdio.h>
static dok_event_bank bank;
static dok_movement_replay_workspace workspace;
int main(void)
{
    dok_movement_replay_report r;int result=dok_movement_replay_run(&r,&bank,&workspace);
    printf("{\"checks\":%lu,\"failures\":%lu,\"first_failed_line\":%lu,\"runs\":%lu,"
           "\"service_calls\":%lu,\"callbacks\":%lu,\"input_samples\":%lu,"
           "\"observations_30\":%lu,\"observations_60\":%lu,\"trace_hash\":%lu,\"final_hash\":%lu,"
           "\"first_elapsed\":%lu,\"short_fire_pulses\":%lu,\"use_pulses\":%lu,\"pause_callbacks\":%lu,"
           "\"workspace_bytes\":%lu,\"final\":{\"x\":%ld,\"y\":%ld,\"z\":%ld,\"heading\":%d,"
           "\"movement_accumulator\":%ld,\"turn_accumulator\":%ld}}\n",
           (unsigned long)r.checks,(unsigned long)r.failures,(unsigned long)r.first_failed_line,
           (unsigned long)r.runs,(unsigned long)r.service_calls,(unsigned long)r.callbacks,
           (unsigned long)r.input_samples,(unsigned long)r.observations_30,(unsigned long)r.observations_60,
           (unsigned long)r.trace_hash,(unsigned long)r.final_hash,(unsigned long)r.first_elapsed,
           (unsigned long)r.short_fire_pulses,(unsigned long)r.use_pulses,(unsigned long)r.pause_callbacks,
           (unsigned long)sizeof(workspace),(long)r.final_motion.x,(long)r.final_motion.y,
           (long)r.final_motion.z,(int)r.final_motion.heading,(long)r.final_motion.movement_accumulator,
           (long)r.final_motion.turn_accumulator);
    return result;
}
