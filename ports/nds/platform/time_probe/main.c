/* N08 diagnostic: real SDK clock, source scheduler and injected test timelines. */
#include <nds.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dok/time.h"
#include "dok/events.h"
#include "dok/memory_budget.h"
#include "time_cases.h"
#include "../time/clock.h"
#include "time_probe.h"

extern unsigned char __end__, __eheap_end;
_Alignas(32) volatile NdsTimeProbe g_nds_time_probe;
_Alignas(32) volatile NdsTimeTraceRecord g_nds_time_trace[NDS_TIME_TRACE_CAPACITY];
_Static_assert(sizeof(NdsTimeProbe) == 228, "N08 probe ABI");
_Static_assert(sizeof(NdsTimeTraceRecord) == 116, "N08 trace ABI");
static NdsTimeProbe obs;
static dok_world_clock world_clock;
static dok_event_scheduler scheduler;
static dok_event_bank *bank;
static unsigned char *managed;
static volatile uint32_t vblanks;
static uint64_t synthetic_counter;
static uint32_t callback_error;
static PrintConsole top, bottom;

static uint32_t address(const void *p) { return (uint32_t)(uintptr_t)p; }
static void on_vblank(void) { ++vblanks; }
static uint64_t read_synthetic(void *context) { (void)context; return synthetic_counter; }

static void snapshot_trace(NdsTimeTraceRecord *row)
{
    uint64_t raw = world_clock._platform.read_counter(world_clock._platform.context);
    row->scenario_id = obs.scenario_id;
    row->sample_index = obs.scenario_sample;
    row->native_now = world_clock._ticks;
    row->paused = world_clock._paused;
    row->raw_counter_low = (uint32_t)raw;
    row->raw_counter_high = (uint32_t)(raw >> 32);
    row->clock_last_counter_low = (uint32_t)world_clock._last_counter;
    row->clock_last_counter_high = (uint32_t)(world_clock._last_counter >> 32);
    row->rate_numerator_low = (uint32_t)world_clock._numerator;
    row->rate_numerator_high = (uint32_t)(world_clock._numerator >> 32);
    row->rate_denominator_low = (uint32_t)world_clock._denominator;
    row->rate_denominator_high = (uint32_t)(world_clock._denominator >> 32);
    row->fraction_low = (uint32_t)world_clock._fraction;
    row->fraction_high = (uint32_t)(world_clock._fraction >> 32);
    row->vblank_count = vblanks;
    row->callback_total = obs.callback_total;
    row->render_reads = obs.render_reads;
}

static void trace(NdsTimeTraceRecord *row)
{
    uint32_t index = obs.trace_total % NDS_TIME_TRACE_CAPACITY;
    snapshot_trace(row);
    row->magic = NDS_TIME_TRACE_MAGIC;
    row->abi_version = NDS_TIME_ABI_VERSION;
    row->size_bytes = sizeof(*row);
    row->ordinal = ++obs.trace_total;
    row->sequence = g_nds_time_trace[index].sequence + 1u;
    g_nds_time_trace[index].sequence = row->sequence;
    DC_FlushRange((const void *)&g_nds_time_trace[index].sequence, 32);
    memcpy((void *)&g_nds_time_trace[index], row, sizeof(*row));
    DC_FlushRange((const void *)&g_nds_time_trace[index], sizeof(*row));
    ++g_nds_time_trace[index].sequence;
    DC_FlushRange((const void *)&g_nds_time_trace[index].sequence, 32);
}

static void boundary(uint32_t action)
{
    NdsTimeTraceRecord row = {0};
    const dok_event_owner *owner;
    row.action = action;
    if (obs.scenario_id == 4u &&
        dok_events_inspect_owner(&scheduler, (dok_object_ref){104, 1, 1}, &owner) == DOK_STATUS_OK && owner->count) {
        row.native_deadline = owner->events[0].deadline;
        row.native_interval = owner->events[0].interval;
    }
    trace(&row);
}

static void publish(void)
{
    NdsTimeTraceRecord row = {0};
    obs.magic = NDS_TIME_PROBE_MAGIC;
    obs.abi_version = NDS_TIME_ABI_VERSION;
    obs.size_bytes = sizeof(obs);
    obs.vblank_count = vblanks;
    if (world_clock._initialized) {
        snapshot_trace(&row);
        obs.counter_hz_numerator = world_clock._platform.counter_hz_numerator;
        obs.counter_hz_denominator = world_clock._platform.counter_hz_denominator;
        obs.raw_counter_low = row.raw_counter_low; obs.raw_counter_high = row.raw_counter_high;
        obs.clock_last_counter_low = row.clock_last_counter_low; obs.clock_last_counter_high = row.clock_last_counter_high;
        obs.rate_numerator_low = row.rate_numerator_low; obs.rate_numerator_high = row.rate_numerator_high;
        obs.rate_denominator_low = row.rate_denominator_low; obs.rate_denominator_high = row.rate_denominator_high;
        obs.fraction_low = row.fraction_low; obs.fraction_high = row.fraction_high;
        obs.native_now = row.native_now; obs.paused = row.paused;
    }
    obs.registered_owners = scheduler._registered_count;
    obs.active_owners = scheduler._owner_count;
    obs.active_events = scheduler._event_count;
    obs.sequence = g_nds_time_probe.sequence + 1u;
    g_nds_time_probe.sequence = obs.sequence;
    DC_FlushRange((const void *)&g_nds_time_probe.sequence, 32);
    memcpy((void *)&g_nds_time_probe, &obs, sizeof(obs));
    DC_FlushRange((const void *)&g_nds_time_probe, sizeof(obs));
    ++g_nds_time_probe.sequence;
    DC_FlushRange((const void *)&g_nds_time_probe.sequence, 32);
}

static int memory_setup(void)
{
    void *allocation;
    obs.region_base = DOK_NDS_MAIN_BASE; obs.region_limit = DOK_NDS_ARM9_LIMIT;
    obs.static_end = address(&__end__); obs.heap_start = address(getHeapStart());
    obs.heap_limit_before = address(getHeapLimit()); obs.is_dsi = isDSiMode();
    obs.reserve_bytes = DOK_NDS_RESERVE_BYTES; obs.sdk_heap_min = DOK_NDS_SDK_HEAP_MIN;
    if (obs.is_dsi || address(&__eheap_end) != DOK_NDS_ARM9_LIMIT ||
        obs.heap_limit_before < DOK_NDS_ARM9_LIMIT ||
        obs.static_end > DOK_NDS_MAIN_BASE + DOK_NDS_STATIC_CEILING) return 0;
    if (obs.heap_limit_before > DOK_NDS_ARM9_LIMIT &&
        reduceHeapSize(obs.heap_limit_before - DOK_NDS_ARM9_LIMIT) != 0) return 0;
    obs.heap_limit_policy = address(getHeapLimit());
    if (reduceHeapSize(DOK_NDS_RESERVE_BYTES) != 0) return 0;
    obs.malloc_request_bytes = DOK_NDS_SLAB_BYTES + 31u;
    allocation = malloc(obs.malloc_request_bytes);
    if (!allocation) return 0;
    managed = (unsigned char *)(((uintptr_t)allocation + 31u) & ~(uintptr_t)31u);
    obs.managed_base = address(managed); obs.managed_bytes = DOK_NDS_SLAB_BYTES;
    bank = (dok_event_bank *)(void *)(managed + DOK_NDS_WORLD_BYTES);
    obs.events_bank_bytes = sizeof(*bank);
    memset((unsigned char *)bank + sizeof(*bank), 0xa5, DOK_NDS_EVENTS_BYTES - sizeof(*bank));
    return 1;
}

static int memory_check(void)
{
    uint32_t i, heap = address(getHeapEnd());
    const unsigned char *tail = (const unsigned char *)bank + sizeof(*bank);
    for (i = 0; i < DOK_NDS_EVENTS_BYTES - sizeof(*bank); ++i)
        if (tail[i] != 0xa5) return 0;
    obs.heap_end = heap; obs.heap_limit_final = address(getHeapLimit());
    if (heap < obs.static_end + DOK_NDS_SLAB_BYTES || heap > obs.heap_limit_final) return 0;
    obs.static_startup_bytes = heap - DOK_NDS_MAIN_BASE - DOK_NDS_SLAB_BYTES;
    obs.sdk_heap_available = obs.heap_limit_final - heap;
    return obs.heap_limit_final == DOK_NDS_ARM9_LIMIT - DOK_NDS_RESERVE_BYTES &&
        obs.static_startup_bytes <= DOK_NDS_STATIC_CEILING &&
        obs.sdk_heap_available >= DOK_NDS_SDK_HEAP_MIN;
}

static void callback(void *context, dok_event_scheduler *events,
                     dok_object_ref reference, uint16_t id, uint32_t parameter)
{
    const dok_event_owner *owner;
    NdsTimeTraceRecord row = {0};
    (void)context;
    if (dok_events_inspect_owner(events, reference, &owner) != DOK_STATUS_OK || !owner->count) {
        callback_error = 1; return;
    }
    row.action = NDS_TIME_ACTION_CALLBACK;
    row.owner = reference.object_id; row.callback = id; row.parameter = parameter;
    row.native_deadline = owner->events[0].deadline;
    row.native_interval = owner->events[0].interval;
    ++obs.callback_total;
    if (obs.scenario_id == 4u) {
        if (obs.status == NDS_TIME_PAUSED) ++obs.paused_callbacks;
        else if (obs.status == NDS_TIME_RESUMED) ++obs.resumed_callbacks;
        else ++obs.live_callbacks;
    }
    trace(&row);
}

static int initialize_events(uint32_t scenario, const dok_platform_services *services)
{
    dok_event_config config = {&world_clock, dok_world_clock_read, NULL, callback, 100u + scenario};
    if (dok_world_clock_init(&world_clock, services, 0) != DOK_STATUS_OK ||
        dok_events_init(&scheduler, bank, &config) != DOK_STATUS_OK) return 0;
    obs.scenario_id = scenario; obs.scenario_sample = 0; obs.render_reads = 0;
    return 1;
}

static int synthetic(void)
{
    static const uint32_t intervals[3] = {416, 1040, 1664};
    static const uint32_t rates[3] = {15, 30, 60};
    const dok_platform_services services = {NULL, read_synthetic, 1193182u, 119u, NULL};
    uint32_t scenario, owner, now, tick;
    dok_event_dispatch_report dispatch_report;
    for (scenario = 1; scenario <= 3; ++scenario) {
        uint32_t last_render = UINT32_MAX;
        synthetic_counter = 0;
        if (!initialize_events(scenario, &services)) return 0;
        for (owner = 1; owner <= 3; ++owner) {
            dok_object_ref reference = {100u + scenario, owner, 1};
            if (dok_events_register(&scheduler, reference, 4) != DOK_STATUS_OK ||
                dok_events_schedule(&scheduler, reference, (uint16_t)owner, 100u + owner,
                                    intervals[owner - 1u]) != DOK_STATUS_OK) return 0;
        }
        for (now = 0; now <= 8000; now += 5) {
            uint32_t render_index = now * rates[scenario - 1u] * 119u / 1193182u;
            synthetic_counter = now; obs.scenario_sample = now / 5u;
            if (dok_events_dispatch(&scheduler, &dispatch_report) != DOK_STATUS_OK || callback_error) return 0;
            if (render_index != last_render) {
                if (dok_world_clock_read(&world_clock, &tick) != DOK_STATUS_OK) return 0;
                ++obs.render_reads; boundary(NDS_TIME_ACTION_RENDER); last_render = render_index;
            }
        }
        boundary(NDS_TIME_ACTION_SCENARIO_END);
    }
    return obs.trace_total < NDS_TIME_TRACE_CAPACITY;
}

static void draw(void)
{
    consoleSelect(&top); consoleClear();
    printf("N08 TIME + EVENTS\n\n");
    printf("checks %lu failures %lu\n", (unsigned long)obs.checks, (unsigned long)obs.failures);
    printf("world ticks %lu\n", (unsigned long)world_clock._ticks);
    printf("state %lu paused %lu\n", (unsigned long)obs.status, (unsigned long)world_clock._paused);
    printf("live %lu resumed %lu\n", (unsigned long)obs.live_callbacks, (unsigned long)obs.resumed_callbacks);
    printf("events bank %lu / 65536\n", (unsigned long)obs.events_bank_bytes);
    printf("RAM reserve %lu\n", (unsigned long)obs.reserve_bytes);
    printf("\nStart: pause / resume\n");
    consoleSelect(&bottom); consoleClear();
    printf("N08 CLOCK OBSERVATION\n\n");
    printf("SDK timer2 / 64\n");
    printf("15/30/60 render reads\n");
    printf("same service timeline\n");
    printf("trace records %lu\n", (unsigned long)obs.trace_total);
    printf("paused callbacks %lu\n", (unsigned long)obs.paused_callbacks);
    printf("\nPhysical DS: N24\n");
}

int main(void)
{
    dok_time_test_report tests = {0};
    dok_event_dispatch_report dispatch_report;
    dok_platform_services services;
    uint32_t resumed_frames = 0, tick;
    if (!memory_setup()) { obs.error_bits = 1; goto done; }
    powerOn(POWER_ALL_2D); lcdMainOnTop();
    videoSetMode(MODE_0_2D); videoSetModeSub(MODE_0_2D);
    vramSetBankA(VRAM_A_MAIN_BG); vramSetBankC(VRAM_C_SUB_BG);
    consoleInit(&top, 0, BgType_Text4bpp, BgSize_T_256x256, 31, 0, true, true);
    consoleInit(&bottom, 0, BgType_Text4bpp, BgSize_T_256x256, 31, 0, false, true);
    irqSet(IRQ_VBLANK, on_vblank); irqEnable(IRQ_VBLANK);
    obs.trace_address = address((const void *)g_nds_time_trace);
    obs.trace_capacity = NDS_TIME_TRACE_CAPACITY; obs.trace_record_bytes = sizeof(NdsTimeTraceRecord);
    obs.status = NDS_TIME_SYNTHETIC; publish();
    (void)dok_time_run_cases(&tests, bank);
    obs.checks = tests.checks; obs.failures = tests.failures;
    obs.first_failed_line = tests.first_failed_line; obs.clock_cases = tests.clock_cases; obs.event_cases = tests.event_cases;
    if (tests.failures) { obs.error_bits = 2; goto done; }
    if (!synthetic()) { obs.error_bits = 4; goto done; }
    if (dok_nds_clock_initialize(&services) != DOK_STATUS_OK || !initialize_events(4, &services) ||
        dok_events_register(&scheduler, (dok_object_ref){104, 1, 1}, 4) != DOK_STATUS_OK ||
        dok_events_schedule(&scheduler, (dok_object_ref){104, 1, 1}, 1, 101, 416) != DOK_STATUS_OK)
        { obs.error_bits = 8; goto done; }
    obs.status = NDS_TIME_LIVE; boundary(NDS_TIME_ACTION_LIVE_BEGIN); publish(); draw();
    for (;;) {
        swiWaitForVBlank(); scanKeys(); ++obs.scenario_sample;
        if (keysDown() & KEY_START) {
            if (obs.status == NDS_TIME_LIVE) {
                if (dok_world_clock_pause(&world_clock) != DOK_STATUS_OK) { obs.error_bits = 16; break; }
                obs.status = NDS_TIME_PAUSED; boundary(NDS_TIME_ACTION_PAUSE); draw();
            } else if (obs.status == NDS_TIME_PAUSED) {
                if (dok_world_clock_resume(&world_clock) != DOK_STATUS_OK) { obs.error_bits = 16; break; }
                obs.status = NDS_TIME_RESUMED; boundary(NDS_TIME_ACTION_RESUME); draw();
            }
        }
        if (obs.status != NDS_TIME_PAUSED &&
            (dok_events_dispatch(&scheduler, &dispatch_report) != DOK_STATUS_OK || callback_error)) { obs.error_bits = 16; break; }
        if (dok_world_clock_read(&world_clock, &tick) != DOK_STATUS_OK) { obs.error_bits = 16; break; }
        boundary(NDS_TIME_ACTION_CLOCK); publish();
        if (obs.status == NDS_TIME_RESUMED && ++resumed_frames >= 96) break;
        if (obs.scenario_sample % 30u == 0) draw();
    }
    if (!memory_check() || obs.paused_callbacks) obs.error_bits |= 32;
done:
    obs.status = obs.error_bits ? NDS_TIME_FAILED : NDS_TIME_COMPLETE;
    if (world_clock._initialized) boundary(NDS_TIME_ACTION_COMPLETE);
    publish();
    if (bank) draw();
    for (;;) { swiWaitForVBlank(); publish(); }
}
