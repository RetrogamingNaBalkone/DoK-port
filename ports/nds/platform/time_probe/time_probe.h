#ifndef DOK_NDS_TIME_PROBE_H
#define DOK_NDS_TIME_PROBE_H

#include <stdint.h>

/* Observation ABI only: scalar little-endian uint32 fields. Each record's
 * sequence is made volatile odd and its cache line flushed BEFORE changing
 * payload. Flush payload, then publish/flush the even sequence. The observer
 * reads sequence/payload/sequence and never writes emulated RAM or clocks. */
#define NDS_TIME_PROBE_MAGIC 0x38544b44u
#define NDS_TIME_TRACE_MAGIC 0x38564b44u
#define NDS_TIME_ABI_VERSION 1u
#define NDS_TIME_TRACE_CAPACITY 512u

enum {
    NDS_TIME_BOOT = 0u,
    NDS_TIME_SYNTHETIC = 1u,
    NDS_TIME_LIVE = 2u,
    NDS_TIME_PAUSED = 3u,
    NDS_TIME_RESUMED = 4u,
    NDS_TIME_COMPLETE = 5u,
    NDS_TIME_FAILED = 6u
};

enum {
    NDS_TIME_ACTION_CLOCK = 1u,
    NDS_TIME_ACTION_CALLBACK = 2u,
    NDS_TIME_ACTION_PAUSE = 3u,
    NDS_TIME_ACTION_RESUME = 4u,
    NDS_TIME_ACTION_RENDER = 5u,
    NDS_TIME_ACTION_SCENARIO_END = 6u,
    NDS_TIME_ACTION_LIVE_BEGIN = 7u,
    NDS_TIME_ACTION_COMPLETE = 8u
};

/* scenario 1/2/3 use the SAME explicit dispatch samples with 15/30/60 render
 * reads; their callback tuples must match. scenario 4 is actual DS time and
 * scheduler period416. Native ticks wrap uint32; raw counters are monotonic
 * uint64 and must not wrap during this service lifetime. */
typedef struct NdsTimeProbe {
    uint32_t magic, abi_version, size_bytes, sequence;
    uint32_t status, error_bits, is_dsi, vblank_count;
    uint32_t counter_hz_numerator, counter_hz_denominator;
    uint32_t raw_counter_low, raw_counter_high;
    uint32_t clock_last_counter_low, clock_last_counter_high;
    uint32_t rate_numerator_low, rate_numerator_high;
    uint32_t rate_denominator_low, rate_denominator_high;
    uint32_t fraction_low, fraction_high, native_now, paused;
    uint32_t checks, failures, first_failed_line, clock_cases, event_cases;
    uint32_t events_bank_bytes, registered_owners, active_owners, active_events;
    uint32_t trace_address, trace_capacity, trace_record_bytes, trace_total;
    uint32_t scenario_id, scenario_sample, render_reads, callback_total;
    uint32_t live_callbacks, paused_callbacks, resumed_callbacks;
    uint32_t region_base, region_limit, static_end, heap_start, heap_end;
    uint32_t heap_limit_before, heap_limit_policy, heap_limit_final;
    uint32_t reserve_bytes, managed_base, managed_bytes, malloc_request_bytes;
    uint32_t static_startup_bytes, sdk_heap_available, sdk_heap_min;
} NdsTimeProbe;

/* clock_last_counter/_fraction/_ticks form an atomic world-clock snapshot.
 * raw_counter may be a later independent platform read. Callback records use
 * pre-action native time; native_deadline/interval describe its scheduled
 * source record. owner/callback/parameter are zero for boundary snapshots;
 * LIVE_BEGIN/PAUSE/RESUME retain the actual pending deadline and interval.
 * ordinal starts at1 and ring slot=(ordinal-1)%capacity. */
typedef struct NdsTimeTraceRecord {
    uint32_t magic, abi_version, size_bytes, sequence;
    uint32_t ordinal, scenario_id, action, sample_index;
    uint32_t owner, callback, parameter, flags;
    uint32_t native_now, native_deadline, native_interval, paused;
    uint32_t raw_counter_low, raw_counter_high;
    uint32_t clock_last_counter_low, clock_last_counter_high;
    uint32_t rate_numerator_low, rate_numerator_high;
    uint32_t rate_denominator_low, rate_denominator_high;
    uint32_t fraction_low, fraction_high;
    uint32_t vblank_count, callback_total, render_reads;
} NdsTimeTraceRecord;

extern volatile NdsTimeProbe g_nds_time_probe;
extern volatile NdsTimeTraceRecord g_nds_time_trace[NDS_TIME_TRACE_CAPACITY];

#endif
