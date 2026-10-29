#ifndef DOK_NDS_MOVEMENT_PROBE_H
#define DOK_NDS_MOVEMENT_PROBE_H

#include <stdint.h>

/* Read-only observation ABI. Publish volatile odd sequence and flush its
 * cacheline BEFORE payload writes; flush payload, then publish/flush even.
 * All signed motion coordinates/WORDs are sign-extended uint32 bit patterns.
 * Ring ordinal starts1; slot=(ordinal-1)%512. Observer never writes RAM. */
#define NDS_MOVEMENT_PROBE_MAGIC 0x394d4b44u
#define NDS_MOVEMENT_TRACE_MAGIC 0x39564b44u
#define NDS_MOVEMENT_ABI_VERSION 1u
#define NDS_MOVEMENT_TRACE_CAPACITY 512u

enum {
    NDS_MOVEMENT_BOOT=0u, NDS_MOVEMENT_LIVE=1u, NDS_MOVEMENT_PAUSED=2u,
    NDS_MOVEMENT_COMPLETE=3u, NDS_MOVEMENT_FAILED=4u
};
enum {
    NDS_MOVEMENT_ACTION_INPUT=1u, NDS_MOVEMENT_ACTION_CALLBACK=2u,
    NDS_MOVEMENT_ACTION_RENDER=3u, NDS_MOVEMENT_ACTION_PAUSE=4u,
    NDS_MOVEMENT_ACTION_RESUME=5u, NDS_MOVEMENT_ACTION_COMPLETE=6u
};

typedef struct NdsMovementProbe {
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
    uint32_t reserved0, reserved1, reserved2;
} NdsMovementProbe;

typedef struct NdsMovementTraceRecord {
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
    uint32_t rate_numerator_low, rate_numerator_high;
    uint32_t rate_denominator_low, rate_denominator_high;
    uint32_t fraction_low, fraction_high, native_now, paused;
    uint32_t callback_total, render_reads, input_total, pause_count, resume_count;
    uint32_t native_deadline, native_interval;
    uint32_t raw_touch_x, raw_touch_y, physical_touch;
    uint32_t input_flags, sample_counter_low, sample_counter_high;
    uint32_t service_tick, watch_start, watch_started;
} NdsMovementTraceRecord;

extern volatile NdsMovementProbe g_nds_movement_probe;
extern volatile NdsMovementTraceRecord g_nds_movement_trace[NDS_MOVEMENT_TRACE_CAPACITY];

#endif
