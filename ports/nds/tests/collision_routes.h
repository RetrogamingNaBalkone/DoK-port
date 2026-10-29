#ifndef DOK_NDS_COLLISION_ROUTES_H
#define DOK_NDS_COLLISION_ROUTES_H

#include "dok/camera.h"
#include "dok/controls.h"
#include "dok/events.h"
#include "dok/time.h"
#include "dok/collision.h"

enum { DOK_COLLISION_ROUTES_SERVICES=128 };
typedef struct dok_collision_routes_report {
    uint32_t checks,failures,first_failed_line,runs;
    uint32_t service_calls,callbacks,input_samples;
    uint32_t observations_30,observations_60,trace_hash,final_hash;
    uint32_t first_elapsed,short_fire_pulses,use_pulses,pause_callbacks;
    dok_motion_state final_motion;
    uint32_t map_hash,counter_hash,requested,committed,adjusted,rejected,camera_fallbacks;
} dok_collision_routes_report;

typedef struct dok_collision_routes_trace {
    dok_motion_state motion;
    dok_camera_history camera;
    dok_event_owner owner;
    dok_control_commands commands;
    uint64_t counter;
    uint32_t service,world_tick,callbacks,last_elapsed;
    uint32_t stopwatch_active,stopwatch_start,paused,input_count;
    uint32_t buffer_raw_held,control_held,map_hash,counter_hash;
    dok_collision_report collision;
} dok_collision_routes_trace;

typedef struct dok_collision_routes_context {
    dok_initial_world *world;
    dok_collision_state *collision;
    dok_collision_context movement;
    dok_collision_report collision_total;
    dok_input_buffer input;
    dok_control_latch controls;
    dok_world_clock clock;
    dok_event_scheduler scheduler;
    dok_motion_state motion;
    dok_camera_history camera;
    dok_control_commands commands;
    uint64_t counter,sequence;
    uint32_t callbacks,last_elapsed,first_elapsed,stopwatch_active,stopwatch_start;
    uint32_t raw_held,raw_touch,paused,errors,input_samples;
    uint32_t short_fire_pulses,use_pulses,pause_callbacks;
    uint32_t ui_guarded_callbacks,ui_action_leaks;
    uint32_t presentation_observations,presentation_hash,camera_fallbacks;
} dok_collision_routes_context;

/* Caller owns this typed workspace in the N03 scratch slab. Its baseline bank
 * is an observational copy, not a second active scheduler. No hidden bank or
 * large automatic array is used. A caller bank supplies active event storage. */
typedef struct dok_collision_routes_workspace {
    dok_collision_routes_context context;
    unsigned char saved_maps[4][DOK_LEVEL_MAX_CELLS];
    unsigned char final_maps[4][DOK_LEVEL_MAX_CELLS];
    dok_collision_state saved_collision,final_collision;
    dok_collision_routes_trace baseline[DOK_COLLISION_ROUTES_SERVICES];
    dok_event_bank baseline_bank;
    dok_input_buffer baseline_input;
    dok_control_latch baseline_controls;
    uint64_t baseline_clock_counter,baseline_clock_fraction;
    uint32_t baseline_clock_tick;
    uint32_t baseline_scheduler[13];
} dok_collision_routes_workspace;
_Static_assert(sizeof(dok_collision_routes_workspace)<=DOK_NDS_SCRATCH_BYTES,
               "N10 route workspace fits caller-owned scratch");

/* Shared host/ARM9 integration replay on a loaded RU level.
 * Requires a quiescent bound collision state. Restores world maps and collision
 * state before returning; event bank/workspace are destructive test storage.
 * All three runs execute the
 * same explicit input/service timeline; runs add 0/30/60Hz camera observations.
 * This test does not define or replace the runtime service cadence. */
int dok_collision_routes_run(dok_collision_routes_report *report,
                            dok_initial_world *world,dok_collision_state *collision,
                            dok_event_bank *bank,
                            dok_collision_routes_workspace *workspace);
#endif
