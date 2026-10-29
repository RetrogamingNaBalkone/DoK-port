#ifndef DOK_NDS_INTERACTION_PROBE_H
#define DOK_NDS_INTERACTION_PROBE_H

#include <stdint.h>

/* Read-only observation ABI. Publish volatile odd sequence and flush its
 * cacheline BEFORE payload writes; flush payload, then publish/flush even.
 * All signed motion coordinates/WORDs are sign-extended uint32 bit patterns.
 * Ring ordinal starts1; slot=(ordinal-1)%128. Observer never writes RAM. */
#define NDS_INTERACTION_PROBE_MAGIC 0x31494b44u
#define NDS_INTERACTION_TRACE_MAGIC 0x31544b44u
#define NDS_INTERACTION_ABI_VERSION 1u
#define NDS_INTERACTION_TRACE_CAPACITY 128u
#define NDS_INTERACTION_SNAPSHOT_MAGIC 0x31534b44u
#define NDS_INTERACTION_SNAPSHOT_CAPACITY 8u

enum {
    NDS_INTERACTION_BOOT=0u, NDS_INTERACTION_LOADING=1u, NDS_INTERACTION_LIVE=2u,
    NDS_INTERACTION_PAUSED=3u, NDS_INTERACTION_COMPLETE=4u, NDS_INTERACTION_FAILED=5u
};
enum {
    NDS_INTERACTION_ACTION_INPUT=1u, NDS_INTERACTION_ACTION_CALLBACK=2u,
    NDS_INTERACTION_ACTION_RENDER=3u, NDS_INTERACTION_ACTION_PAUSE=4u,
    NDS_INTERACTION_ACTION_RESUME=5u, NDS_INTERACTION_ACTION_LEVEL_BEGIN=6u,
    NDS_INTERACTION_ACTION_LEVEL_END=7u, NDS_INTERACTION_ACTION_COMPLETE=8u,
    NDS_INTERACTION_ACTION_FIXTURE=9u, NDS_INTERACTION_ACTION_STAGE=10u,
    NDS_INTERACTION_ACTION_USE=11u, NDS_INTERACTION_ACTION_OPEN=12u,
    NDS_INTERACTION_ACTION_CLOSE=13u, NDS_INTERACTION_ACTION_AUTO=14u,
    NDS_INTERACTION_ACTION_FLOOR=15u, NDS_INTERACTION_ACTION_SWEEP=16u,
    NDS_INTERACTION_ACTION_HERO_BEGIN=17u
};

enum {
    NDS_INTERACTION_CASE_LOCKED_A=1u, NDS_INTERACTION_CASE_MIRROR_D=2u,
    NDS_INTERACTION_CASE_VERTICAL_G=3u, NDS_INTERACTION_CASE_MANUAL_H=4u,
    NDS_INTERACTION_CASE_FLOOR_L2=5u, NDS_INTERACTION_CASE_FLOOR_L3=6u
};
enum {
    NDS_INTERACTION_WAIT_LOCKED=1u, NDS_INTERACTION_WAIT_KEY_USE=2u,
    NDS_INTERACTION_WAIT_USE=3u, NDS_INTERACTION_OPENING=4u,
    NDS_INTERACTION_OPEN_OCCUPIED=5u, NDS_INTERACTION_AUTO_BLOCKED=6u,
    NDS_INTERACTION_AUTO_WAIT=7u, NDS_INTERACTION_WAIT_CLOSE_USE=8u,
    NDS_INTERACTION_CLOSE_BLOCKED=9u, NDS_INTERACTION_CLOSING=10u,
    NDS_INTERACTION_CLOSED=11u, NDS_INTERACTION_SENSOR_FIRST=12u,
    NDS_INTERACTION_SENSOR_SAME=13u, NDS_INTERACTION_SENSOR_OUT=14u,
    NDS_INTERACTION_SENSOR_REENTER=15u, NDS_INTERACTION_CONVEYOR=16u,
    NDS_INTERACTION_FINISH_WAIT=17u, NDS_INTERACTION_FINISH_SENT=18u
};
/* Native contract input[17], matching interaction_dump:
 * op,flags,phase,state,ready,occupancy,extent,key,mode,delay,interval,
 * bits,keys,source_class,C2,marker,initial_flags. after/before[18]:
 * flags,phase,state,ready,occupancy,extent,interval,bits,marker,queue_count,
 * then three callback(1=OPEN/2=CLOSE/3=AUTO),interval pairs, two zero words.
 * Queue arrays are raw callback-boundary views, before N08 compaction.
 * native_keep records the pending cancellation; deadlines are separate. */
#define NDS_INTERACTION_NATIVE_INPUT_WORDS 17u
#define NDS_INTERACTION_NATIVE_OUTPUT_WORDS 18u

typedef struct NdsInteractionProbe {
    uint32_t magic, abi_version, size_bytes, sequence;
    uint32_t status, error_bits, is_dsi, vblank_count;
    uint32_t checks, failures, first_failed_line, input_checks, motion_checks, replay_checks;
    uint32_t frame_id, input_total, callback_total, render_reads, pause_count, resume_count;
    uint32_t use_total, next_weapon_total, buttons_seen, touch_seen;
    uint32_t trace_address, trace_capacity, trace_record_bytes, trace_total;
    uint32_t counter_hz_numerator, counter_hz_denominator;
    uint32_t raw_counter_low, raw_counter_high;
    uint32_t clock_last_counter_low, clock_last_counter_high;
    uint32_t rate_numerator_low, rate_numerator_high;
    uint32_t rate_denominator_low, rate_denominator_high;
    uint32_t fraction_low, fraction_high, native_now, paused;
    uint32_t region_base, region_limit, static_end, heap_start, heap_end;
    uint32_t heap_limit_before, heap_limit_policy, heap_limit_final;
    uint32_t reserve_bytes, managed_base, managed_bytes, malloc_request_bytes;
    uint32_t static_startup_bytes, sdk_heap_available, sdk_heap_min;
    uint32_t world_used, scratch_used, scene_used, assets_used;
    uint32_t world_high_water, scratch_high_water, scene_high_water, assets_high_water;
    uint32_t render_overflows, matrix_errors, presented_frames, events_bank_bytes;
    uint32_t interaction_checks, level_id, world_epoch, level_callback_total, total_frames;
    uint32_t snapshot_address, snapshot_capacity, snapshot_record_bytes, snapshot_total;
    uint32_t hero_id, hero_generation, hero_glyph;
    uint32_t collision_requested, collision_committed, collision_adjusted, collision_rejected, collision_reason_mask;
    uint32_t case_id,stage_id,stage_sequence,stage_started;
    uint32_t c2,sweep_ticks,sweep_begin,sweep_end,sweep_total;
    uint32_t keys,score,door_glyph,interaction_revision,phase_revision;
    uint32_t locked_total,finish_total,door_callbacks,sensor_entries;
    uint32_t stage_advances,fixture_occupancy,door_render_checks;
    uint32_t door_phase,door_extent,door_state,door_ready;
    uint32_t rendered_doors,rendered_cropped,rendered_hidden,advance_ready;
} NdsInteractionProbe;

typedef struct NdsInteractionTraceRecord {
    uint32_t magic, abi_version, size_bytes, sequence;
    uint32_t ordinal, action, frame_id, vblank_count, status;
    uint32_t flags, x, y, z, heading, angle_parameter, speed, distance;
    uint32_t turn_step, linear_step, last_turn, movement_accumulator, turn_accumulator;
    uint32_t accel_sign, last_flags, moved, substeps;
    uint32_t service_elapsed, first_service, native_commands;
    uint32_t physical_held, physical_pressed, physical_released;
    uint32_t use_triggered, next_weapon_pressed;
    uint32_t camera_x, camera_y, camera_z, camera_heading_q16;
    uint32_t raw_held, raw_pressed, raw_released, raw_touch;
    uint32_t raw_sequence_low, raw_sequence_high;
    uint32_t raw_counter_low, raw_counter_high;
    uint32_t clock_last_counter_low, clock_last_counter_high;
    uint32_t fraction_low, fraction_high, native_now, paused;
    uint32_t callback_total, render_reads, input_total, pause_count, resume_count;
    uint32_t native_deadline, native_interval;
    uint32_t raw_touch_x, raw_touch_y, physical_touch;
    uint32_t input_flags, sample_counter_low, sample_counter_high;
    uint32_t service_tick, watch_start, watch_started;
    uint32_t level_id, world_epoch, level_callback_total;
    uint32_t hero_id, hero_generation, hero_glyph, upper_y, lower_y;
    uint32_t collision_requested, collision_committed, collision_adjusted, collision_rejected, collision_reason_mask;
    uint32_t camera_alpha_q16, camera_previous_tick, camera_current_tick, camera_fallback, camera_admitted;
    uint32_t snapshot_ordinal, snapshot_slot;
    uint32_t case_id,stage_id,stage_sequence,stage_started;
    uint32_t c2,sweep_ticks,sweep_begin,sweep_end,sweep_total;
    uint32_t keys,score,door_glyph,interaction_revision,phase_revision;
    uint32_t locked_total,finish_total,door_callbacks,sensor_entries;
    uint32_t fixture_occupancy,rendered_doors,rendered_cropped,rendered_hidden,advance_ready;
    uint32_t native_valid,native_keep,native_target_glyph;
    uint32_t native_input[NDS_INTERACTION_NATIVE_INPUT_WORDS];
    uint32_t native_before[NDS_INTERACTION_NATIVE_OUTPUT_WORDS];
    uint32_t native_after[NDS_INTERACTION_NATIVE_OUTPUT_WORDS];
    uint32_t queue_before_deadline[3],queue_after_deadline[3];
} NdsInteractionTraceRecord;

/* Publish each LEVEL_BEGIN and CALLBACK snapshot before its trace. Slots are
 * independent seqlocks; slot=(ordinal-1)%8, ordinal starts1. Maps are packed
 * MMap,TMap,FCMap,visited with4096-byte strides, unused cells zero. Flags and
 * counters are LE unsignedWORDs. Header is exactly64bytes, record19520bytes, including door phase/state. */
typedef struct NdsInteractionSnapshotHeader {
    uint32_t magic, abi_version, size_bytes, sequence;
    uint32_t ordinal, level_id, world_epoch, callback_total;
    uint32_t trace_ordinal, width, length, hero_glyph;
    uint32_t hero_id, hero_generation, case_id, stage_sequence;
} NdsInteractionSnapshotHeader;
typedef struct NdsInteractionSnapshot {
    NdsInteractionSnapshotHeader header;
    unsigned char maps[4][4096];
    uint16_t flags[256], door_count[256];
    unsigned char known[256];
    int16_t phase[256],extent[256];
    unsigned char state[256],ready[256],marker[256];
} NdsInteractionSnapshot;
_Static_assert(sizeof(NdsInteractionSnapshotHeader)==64u,"N11 snapshot header ABI");
_Static_assert(sizeof(NdsInteractionSnapshot)==19520u,"N11 snapshot payload ABI");
_Static_assert(sizeof(NdsInteractionTraceRecord)<=768u,"N11 trace static budget");
extern volatile NdsInteractionProbe g_nds_interaction_probe;
extern volatile NdsInteractionTraceRecord g_nds_interaction_trace[NDS_INTERACTION_TRACE_CAPACITY];
extern volatile NdsInteractionSnapshot g_nds_interaction_snapshot[NDS_INTERACTION_SNAPSHOT_CAPACITY];

#endif
