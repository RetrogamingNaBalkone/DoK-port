#include "time_cases.h"
#include "dok/time.h"

#include <stddef.h>
#include <string.h>

typedef struct test_clock {
    uint64_t counter;
    uint32_t reads;
    uint32_t fail_at;
    uint32_t now;
    uint32_t advance;
    uint32_t mode;
    uint32_t callback_count;
    uint32_t callback_ids[32];
    uint32_t callback_owners[32];
    uint32_t callback_times[32];
    uint32_t helper_failures;
} test_clock;

typedef struct test_state {
    dok_time_test_report *report;
    dok_event_bank *bank;
    dok_event_scheduler scheduler;
    test_clock source;
} test_state;

#define CHECK(s, condition) do { \
    ++(s)->report->checks; \
    if (!(condition)) { \
        ++(s)->report->failures; \
        if ((s)->report->first_failed_line == 0u) { \
            (s)->report->first_failed_line = (uint32_t)__LINE__; \
        } \
        return; \
    } \
} while (0)

static dok_object_ref ref(uint32_t id)
{
    dok_object_ref result = {1u, id, 1u};
    return result;
}

static uint64_t counter(void *context)
{
    test_clock *clock = context;
    ++clock->reads;
    return clock->counter;
}

static dok_status ticks(void *context, uint32_t *out)
{
    test_clock *clock = context;
    ++clock->reads;
    if (clock->reads == clock->fail_at) {
        return DOK_STATUS_INVALID_STATE;
    }
    *out = clock->now;
    return DOK_STATUS_OK;
}

static void helper(test_clock *clock, dok_status actual, dok_status wanted)
{
    if (actual != wanted) {
        ++clock->helper_failures;
    }
}

static void callback(void *context, dok_event_scheduler *scheduler,
                     dok_object_ref owner, uint16_t id, uint32_t parameter)
{
    test_clock *clock = context;
    uint32_t i = clock->callback_count++;
    if (i < 32u) {
        clock->callback_ids[i] = id;
        clock->callback_owners[i] = owner.object_id;
        clock->callback_times[i] = clock->now;
    }
    clock->now += clock->advance;
    if (clock->mode == 1u || (clock->mode == 2u && id == 1u)) {
        helper(clock, dok_events_cancel_current(scheduler), DOK_STATUS_OK);
    }
    if (clock->mode == 2u && id == 2u) {
        helper(clock, dok_events_set_interval(scheduler, 777u), DOK_STATUS_OK);
    }
    if (clock->mode == 3u && id == 1u) {
        helper(clock, dok_events_schedule(scheduler, owner, 3u, 44u, 0u),
               DOK_STATUS_OK);
    }
    if (clock->mode == 4u) {
        helper(clock, dok_events_destroy(scheduler, owner), DOK_STATUS_OK);
        helper(clock, dok_events_schedule(scheduler, owner, 3u, 0u, 0u),
               DOK_STATUS_STALE_REFERENCE);
    }
    if ((clock->mode == 5u || clock->mode == 6u) && owner.object_id == 1u) {
        helper(clock, dok_events_schedule(scheduler, ref(2u), 2u, 0u, 0u),
               DOK_STATUS_OK);
        if (clock->mode == 6u) {
            helper(clock, dok_events_destroy(scheduler, ref(2u)), DOK_STATUS_OK);
        }
        helper(clock, dok_events_cancel_current(scheduler), DOK_STATUS_OK);
    }
    if (clock->mode == 7u) {
        dok_event_dispatch_report ignored;
        helper(clock, dok_events_dispatch(scheduler, &ignored),
               DOK_STATUS_INVALID_STATE);
        helper(clock, dok_events_clear(scheduler), DOK_STATUS_INVALID_STATE);
        helper(clock, dok_events_reset(scheduler, 2u), DOK_STATUS_INVALID_STATE);
        helper(clock, dok_events_register(scheduler, ref(9u), 4u),
               DOK_STATUS_INVALID_STATE);
    }
    if (parameter == UINT32_C(0xBADF00D)) {
        helper(clock, dok_events_destroy(scheduler, ref(1u)), DOK_STATUS_OK);
    }
}

static void setup(test_state *state)
{
    dok_event_config config;
    memset(&state->source, 0, sizeof(state->source));
    config.clock_context = &state->source;
    config.read_ticks = ticks;
    config.callback_context = &state->source;
    config.callback = callback;
    config.world_epoch = 1u;
    (void)dok_events_init(&state->scheduler, state->bank, &config);
}

static void clock_partition(test_state *s)
{
    dok_world_clock clock;
    dok_platform_services platform = {&s->source, counter, 33513982u, 64u, NULL};
    uint32_t out, i;
    uint64_t total;
    setup(s);
    ++s->report->clock_cases;
    CHECK(s, dok_world_clock_init(&clock, &platform, 0u) == DOK_STATUS_OK);
    for (i = 0u; i < 4096u; ++i) {
        s->source.counter += (i * 17u) % 1301u;
        total = s->source.counter * UINT64_C(38181824);
        CHECK(s, dok_world_clock_read(&clock, &out) == DOK_STATUS_OK);
        CHECK(s, out == (uint32_t)(total / UINT64_C(1994081929)));
        CHECK(s, clock._fraction == total % UINT64_C(1994081929));
    }
    s->source.counter -= 1u;
    out = 12345u;
    total = clock._last_counter;
    CHECK(s, dok_world_clock_read(&clock, &out) == DOK_STATUS_INVALID_STATE);
    CHECK(s, out == 12345u && clock._last_counter == total);
}

static void clock_extremes(test_state *s)
{
    static const uint32_t num[4] = {33513982u, 1u, UINT32_MAX, UINT32_MAX};
    static const uint32_t den[4] = {64u, UINT32_MAX, UINT32_MAX, 1u};
    static const uint32_t wanted[4] = {1759966625u, 2346000566u,
                                     1335399972u, 3176120296u};
    static const uint64_t fractions[4] = {1547365143u, 4u, 102u,
                                         UINT64_C(25769803770)};
    uint32_t i, out;
    dok_world_clock clock, before;
    dok_platform_services platform = {&s->source, counter, 1u, 1u, NULL};
    ++s->report->clock_cases;
    for (i = 0u; i < 4u; ++i) {
        setup(s);
        platform.counter_hz_numerator = num[i];
        platform.counter_hz_denominator = den[i];
        CHECK(s, dok_world_clock_init(&clock, &platform, 0u) == DOK_STATUS_OK);
        s->source.counter = UINT64_MAX;
        CHECK(s, dok_world_clock_read(&clock, &out) == DOK_STATUS_OK);
        CHECK(s, out == wanted[i] && clock._fraction == fractions[i]);
    }
    before = clock;
    platform.counter_hz_numerator = 0u;
    CHECK(s, dok_world_clock_init(&clock, &platform, 0u) ==
             DOK_STATUS_INVALID_ARGUMENT);
    CHECK(s, memcmp(&before, &clock, sizeof(clock)) == 0);
}

static void clock_pause(test_state *s)
{
    dok_world_clock clock;
    dok_platform_services platform = {&s->source, counter, 1193182u, 119u, NULL};
    uint32_t out, reads;
    uint64_t fraction;
    setup(s);
    ++s->report->clock_cases;
    CHECK(s, dok_world_clock_init(&clock, &platform, UINT32_MAX - 10u) ==
             DOK_STATUS_OK);
    s->source.counter = 7u;
    CHECK(s, dok_world_clock_pause(&clock) == DOK_STATUS_OK);
    CHECK(s, clock._ticks == UINT32_MAX - 3u);
    reads = s->source.reads;
    CHECK(s, dok_world_clock_pause(&clock) == DOK_STATUS_OK);
    CHECK(s, s->source.reads == reads);
    s->source.counter = 999999u;
    CHECK(s, dok_world_clock_read(&clock, &out) == DOK_STATUS_OK);
    CHECK(s, out == UINT32_MAX - 3u);
    CHECK(s, dok_world_clock_resume(&clock) == DOK_STATUS_OK);
    s->source.counter += 9u;
    CHECK(s, dok_world_clock_read(&clock, &out) == DOK_STATUS_OK);
    CHECK(s, out == 5u);
    platform.counter_hz_numerator = 33513982u;
    platform.counter_hz_denominator = 64u;
    s->source.counter = 0u;
    CHECK(s, dok_world_clock_init(&clock, &platform, 0u) == DOK_STATUS_OK);
    s->source.counter = 3u;
    CHECK(s, dok_world_clock_pause(&clock) == DOK_STATUS_OK);
    fraction = clock._fraction;
    s->source.counter = 1000000u;
    CHECK(s, dok_world_clock_resume(&clock) == DOK_STATUS_OK);
    CHECK(s, clock._fraction == fraction);
    s->source.counter += 50u;
    CHECK(s, dok_world_clock_read(&clock, &out) == DOK_STATUS_OK);
    CHECK(s, out == 1u && clock._fraction ==
             UINT64_C(53) * 38181824u % 1994081929u);
}

static void capacity_lifecycle(test_state *s)
{
    uint32_t id, reads;
    const dok_event_owner *owner = NULL;
    dok_object_ref stale = ref(1u);
    setup(s);
    ++s->report->event_cases;
    for (id = 1u; id <= DOK_EVENT_OWNER_LIMIT; ++id) {
        CHECK(s, dok_events_register(&s->scheduler, ref(id), 4u) == DOK_STATUS_OK);
    }
    CHECK(s, dok_events_register(&s->scheduler, ref(513u), 4u) == DOK_STATUS_NO_SPACE);
    CHECK(s, dok_events_register(&s->scheduler, ref(1u), 4u) ==
             DOK_STATUS_INVALID_STATE);
    for (id = 1u; id <= 3u; ++id) {
        CHECK(s, dok_events_schedule(&s->scheduler, ref(1u), (uint16_t)id,
                                      100u + id, id) == DOK_STATUS_OK);
    }
    reads = s->source.reads;
    CHECK(s, dok_events_schedule(&s->scheduler, ref(1u), 4u, 0u, 0u) ==
             DOK_STATUS_NO_SPACE);
    CHECK(s, s->source.reads == reads && s->scheduler._event_count == 3u);
    stale.generation = 2u;
    CHECK(s, dok_events_destroy(&s->scheduler, stale) == DOK_STATUS_STALE_REFERENCE);
    CHECK(s, dok_events_cancel_current(&s->scheduler) == DOK_STATUS_INVALID_STATE);
    CHECK(s, dok_events_set_interval(&s->scheduler, 1u) == DOK_STATUS_INVALID_STATE);
    CHECK(s, dok_events_clear(&s->scheduler) == DOK_STATUS_OK);
    CHECK(s, s->scheduler._event_count == 0u && s->scheduler._owner_count == 0u);
    CHECK(s, s->scheduler._registered_count == 512u);
    CHECK(s, dok_events_inspect_owner(&s->scheduler, ref(1u), &owner) == DOK_STATUS_OK);
    CHECK(s, owner->count == 0u && owner->capacity == 4u);
    CHECK(s, dok_events_reset(&s->scheduler, 2u) == DOK_STATUS_OK);
    CHECK(s, dok_events_inspect_owner(&s->scheduler, ref(1u), &owner) ==
             DOK_STATUS_STALE_REFERENCE);
    CHECK(s, s->scheduler._registered_count == 0u);
    CHECK(s, dok_events_reset(&s->scheduler, 2u) == DOK_STATUS_LIMIT_REACHED);
    setup(s);
    for (id = 0u; id <= 4u; ++id) {
        CHECK(s, dok_events_register(&s->scheduler, ref(id + 1u), (uint16_t)id) ==
                 DOK_STATUS_OK);
        CHECK(s, dok_events_schedule(&s->scheduler, ref(id + 1u), 1u, 0u, 0u) ==
                 (id < 2u ? DOK_STATUS_NO_SPACE : DOK_STATUS_OK));
    }
}

static void native_quirks(test_state *s)
{
    dok_event_dispatch_report report;
    const dok_event_owner *owner;
    uint32_t i;
    setup(s);
    ++s->report->event_cases;
    for (i = 1u; i <= 3u; ++i) {
        CHECK(s, dok_events_register(&s->scheduler, ref(i), 4u) == DOK_STATUS_OK);
        CHECK(s, dok_events_schedule(&s->scheduler, ref(i), (uint16_t)i, 0u, 0u) ==
                 DOK_STATUS_OK);
    }
    CHECK(s, dok_events_dispatch(&s->scheduler, &report) == DOK_STATUS_OK);
    CHECK(s, report.callbacks == 3u && report.clock_reads == 8u);
    CHECK(s, s->source.callback_owners[0] == 3u &&
             s->source.callback_owners[1] == 1u && s->source.callback_owners[2] == 2u);
    s->source.callback_count = 0u;
    s->source.mode = 1u;
    CHECK(s, dok_events_dispatch(&s->scheduler, &report) == DOK_STATUS_OK);
    CHECK(s, report.callbacks == 2u && s->scheduler._owner_count == 1u);
    CHECK(s, s->source.callback_owners[0] == 3u && s->source.callback_owners[1] == 1u);
    CHECK(s, dok_events_dispatch(&s->scheduler, &report) == DOK_STATUS_OK);
    CHECK(s, report.callbacks == 1u && s->scheduler._event_count == 0u);
    setup(s);
    CHECK(s, dok_events_register(&s->scheduler, ref(1u), 4u) == DOK_STATUS_OK);
    for (i = 1u; i <= 3u; ++i) {
        CHECK(s, dok_events_schedule(&s->scheduler, ref(1u), (uint16_t)i, 0u,
                                      i * 10u) == DOK_STATUS_OK);
    }
    s->source.now = 500u;
    s->source.mode = 2u;
    CHECK(s, dok_events_dispatch(&s->scheduler, &report) == DOK_STATUS_OK);
    CHECK(s, dok_events_inspect_owner(&s->scheduler, ref(1u), &owner) == DOK_STATUS_OK);
    CHECK(s, owner->count == 2u && owner->events[0].callback == 2u);
    CHECK(s, owner->events[0].deadline == 20u && owner->events[0].interval == 777u);
    CHECK(s, owner->events[1].callback == 3u && owner->events[1].deadline == 30u);
    CHECK(s, s->source.helper_failures == 0u);
}

static void repeat_wrap(test_state *s)
{
    dok_event_dispatch_report report;
    const dok_event_owner *owner;
    setup(s);
    ++s->report->event_cases;
    CHECK(s, dok_events_register(&s->scheduler, ref(1u), 4u) == DOK_STATUS_OK);
    s->source.now = 10u;
    CHECK(s, dok_events_schedule(&s->scheduler, ref(1u), 1u, 0u, 5u) == DOK_STATUS_OK);
    s->source.now = 100u;
    s->source.advance = 7u;
    CHECK(s, dok_events_dispatch(&s->scheduler, &report) == DOK_STATUS_OK);
    CHECK(s, report.callbacks == 1u && s->source.now == 107u);
    CHECK(s, dok_events_inspect_owner(&s->scheduler, ref(1u), &owner) == DOK_STATUS_OK);
    CHECK(s, owner->events[0].deadline == 112u);
    CHECK(s, dok_events_dispatch(&s->scheduler, &report) == DOK_STATUS_OK);
    CHECK(s, report.callbacks == 0u);
    CHECK(s, dok_events_clear(&s->scheduler) == DOK_STATUS_OK);
    s->source.advance = 0u;
    s->source.now = UINT32_MAX - 15u;
    CHECK(s, dok_events_schedule(&s->scheduler, ref(1u), 1u, 0u, 32u) == DOK_STATUS_OK);
    CHECK(s, dok_events_dispatch(&s->scheduler, &report) == DOK_STATUS_OK);
    CHECK(s, report.callbacks == 1u); /* Native unsigned early firing. */
    s->source.now = 0u;
    CHECK(s, dok_events_dispatch(&s->scheduler, &report) == DOK_STATUS_OK);
    CHECK(s, report.callbacks == 0u);
    CHECK(s, dok_events_clear(&s->scheduler) == DOK_STATUS_OK);
    CHECK(s, dok_events_schedule(&s->scheduler, ref(1u), 1u, 0u, UINT32_MAX) ==
             DOK_STATUS_OK);
    CHECK(s, dok_events_dispatch(&s->scheduler, &report) == DOK_STATUS_OK);
    CHECK(s, report.callbacks == 0u);
    CHECK(s, dok_events_clear(&s->scheduler) == DOK_STATUS_OK);
    s->source.now = 10u;
    CHECK(s, dok_events_schedule(&s->scheduler, ref(1u), 1u, 0u, UINT32_MAX) ==
             DOK_STATUS_OK);
    CHECK(s, dok_events_dispatch(&s->scheduler, &report) == DOK_STATUS_OK);
    CHECK(s, report.callbacks == 1u);
}

static void callback_lifecycle(test_state *s)
{
    dok_event_dispatch_report report;
    uint32_t mode;
    ++s->report->event_cases;
    for (mode = 3u; mode <= 7u; ++mode) {
        setup(s);
        CHECK(s, dok_events_register(&s->scheduler, ref(1u), 4u) == DOK_STATUS_OK);
        CHECK(s, dok_events_register(&s->scheduler, ref(2u), 4u) == DOK_STATUS_OK);
        CHECK(s, dok_events_schedule(&s->scheduler, ref(1u), 1u, 0u, 0u) == DOK_STATUS_OK);
        s->source.mode = mode;
        CHECK(s, dok_events_dispatch(&s->scheduler, &report) == DOK_STATUS_OK);
        CHECK(s, s->source.helper_failures == 0u);
        if (mode == 3u) {
            CHECK(s, report.callbacks == 2u && s->source.callback_ids[1] == 3u);
        } else if (mode == 4u) {
            CHECK(s, report.callbacks == 1u && s->scheduler._event_count == 0u);
            CHECK(s, s->scheduler._registered_count == 1u);
        } else if (mode == 5u) {
            CHECK(s, report.callbacks == 1u && s->scheduler._owner_count == 1u);
            CHECK(s, dok_events_dispatch(&s->scheduler, &report) == DOK_STATUS_OK);
            CHECK(s, report.callbacks == 1u && s->source.callback_owners[1] == 2u);
        } else if (mode == 6u) {
            CHECK(s, report.callbacks == 1u && s->scheduler._event_count == 0u);
            CHECK(s, s->scheduler._registered_count == 1u);
        }
    }
}

static void clock_fault(test_state *s)
{
    dok_event_dispatch_report report;
    const dok_event_owner *owner;
    setup(s);
    ++s->report->event_cases;
    CHECK(s, dok_events_register(&s->scheduler, ref(1u), 4u) == DOK_STATUS_OK);
    CHECK(s, dok_events_schedule(&s->scheduler, ref(1u), 1u, 0u, 0u) == DOK_STATUS_OK);
    CHECK(s, dok_events_schedule(&s->scheduler, ref(1u), 2u, 0u, 0u) == DOK_STATUS_OK);
    s->source.fail_at = s->source.reads + 3u; /* Initial,due,post-callback. */
    CHECK(s, dok_events_dispatch(&s->scheduler, &report) == DOK_STATUS_INVALID_STATE);
    CHECK(s, report.callbacks == 1u && report.clock_reads == 3u);
    CHECK(s, dok_events_inspect_owner(&s->scheduler, ref(1u), &owner) == DOK_STATUS_OK);
    CHECK(s, owner->count == 2u && owner->events[1].callback == 2u);
    CHECK(s, s->scheduler._event_count == 2u && s->scheduler._dispatching == 0u);
    CHECK(s, dok_events_dispatch(&s->scheduler, &report) == DOK_STATUS_INVALID_STATE);
    CHECK(s, dok_events_clear(&s->scheduler) == DOK_STATUS_INVALID_STATE);
    CHECK(s, dok_events_reset(&s->scheduler, 2u) == DOK_STATUS_OK);
}

static void mid_dispatch_rollover(test_state *s)
{
    dok_event_dispatch_report report;
    setup(s);
    ++s->report->event_cases;
    CHECK(s, dok_events_register(&s->scheduler, ref(1u), 4u) == DOK_STATUS_OK);
    s->source.now = UINT32_MAX - 15u;
    CHECK(s, dok_events_schedule(&s->scheduler, ref(1u), 1u, 0u, 0u) == DOK_STATUS_OK);
    s->source.advance = 32u;
    s->source.mode = 1u; /* Cancel the final event while the clock wraps. */
    CHECK(s, dok_events_dispatch(&s->scheduler, &report) == DOK_STATUS_OK);
    CHECK(s, report.owners_visited == 1u && report.slots_examined == 1u);
    CHECK(s, report.callbacks == 1u && report.clock_reads == 4u);
    CHECK(s, s->source.now == 16u && s->source.helper_failures == 0u);
    CHECK(s, s->scheduler._event_count == 0u && s->scheduler._owner_count == 0u);
    CHECK(s, s->scheduler._cursor == UINT16_MAX && s->scheduler._dispatching == 0u);
    CHECK(s, dok_events_dispatch(&s->scheduler, &report) == DOK_STATUS_OK);
    CHECK(s, report.callbacks == 0u && report.clock_reads == 0u);
}

static void presentation_separation(test_state *s)
{
    uint32_t baseline_ids[32], baseline_owners[32], baseline_times[32];
    uint32_t baseline_count = 0u;
    uint32_t rate, now, id, observed, out;
    dok_world_clock world_clock;
    dok_platform_services platform = {&s->source, counter, 1193182u, 119u, NULL};
    dok_event_config config;
    dok_event_dispatch_report report;
    ++s->report->event_cases;
    for (rate = 15u; rate <= 60u; rate *= 2u) {
        setup(s);
        CHECK(s, dok_world_clock_init(&world_clock, &platform, 0u) == DOK_STATUS_OK);
        config = s->scheduler._config;
        config.clock_context = &world_clock;
        config.read_ticks = dok_world_clock_read;
        CHECK(s, dok_events_init(&s->scheduler, s->bank, &config) == DOK_STATUS_OK);
        for (id = 1u; id <= 3u; ++id) {
            CHECK(s, dok_events_register(&s->scheduler, ref(id), 4u) == DOK_STATUS_OK);
            CHECK(s, dok_events_schedule(&s->scheduler, ref(id), (uint16_t)id,
                                          0u, id == 1u ? 416u :
                                          (id == 2u ? 1040u : 1664u)) == DOK_STATUS_OK);
        }
        observed = 0u;
        for (now = 0u; now <= 8000u; now += 5u) {
            uint32_t presentation = (uint32_t)
                ((uint64_t)now * rate * 119u / 1193182u);
            s->source.counter = now;
            s->source.now = now;
            CHECK(s, dok_events_dispatch(&s->scheduler, &report) == DOK_STATUS_OK);
            if (presentation != observed) {
                uint32_t callbacks_before = s->source.callback_count;
                CHECK(s, dok_world_clock_read(&world_clock, &out) == DOK_STATUS_OK);
                CHECK(s, out == now && s->source.callback_count == callbacks_before);
                observed = presentation;
            }
        }
        CHECK(s, s->source.callback_count == 30u);
        if (rate == 15u) {
            baseline_count = s->source.callback_count;
            memcpy(baseline_ids, s->source.callback_ids, sizeof(baseline_ids));
            memcpy(baseline_owners, s->source.callback_owners, sizeof(baseline_owners));
            memcpy(baseline_times, s->source.callback_times, sizeof(baseline_times));
        } else {
            CHECK(s, s->source.callback_count == baseline_count);
            CHECK(s, memcmp(baseline_ids, s->source.callback_ids,
                             sizeof(baseline_ids)) == 0);
            CHECK(s, memcmp(baseline_owners, s->source.callback_owners,
                             sizeof(baseline_owners)) == 0);
            CHECK(s, memcmp(baseline_times, s->source.callback_times,
                             sizeof(baseline_times)) == 0);
        }
    }
    /* Paused world time cannot make an event overdue even if sampled in menu. */
    setup(s);
    CHECK(s, dok_world_clock_init(&world_clock, &platform, 0u) == DOK_STATUS_OK);
    config = s->scheduler._config;
    config.clock_context = &world_clock;
    config.read_ticks = dok_world_clock_read;
    CHECK(s, dok_events_init(&s->scheduler, s->bank, &config) == DOK_STATUS_OK);
    CHECK(s, dok_events_register(&s->scheduler, ref(1u), 4u) == DOK_STATUS_OK);
    CHECK(s, dok_events_schedule(&s->scheduler, ref(1u), 1u, 0u, 100u) == DOK_STATUS_OK);
    s->source.counter = 99u;
    CHECK(s, dok_world_clock_pause(&world_clock) == DOK_STATUS_OK);
    s->source.counter = UINT64_C(4000000000);
    CHECK(s, dok_events_dispatch(&s->scheduler, &report) == DOK_STATUS_OK);
    CHECK(s, report.callbacks == 0u && world_clock._ticks == 99u);
    CHECK(s, dok_world_clock_resume(&world_clock) == DOK_STATUS_OK);
    ++s->source.counter;
    CHECK(s, dok_events_dispatch(&s->scheduler, &report) == DOK_STATUS_OK);
    CHECK(s, report.callbacks == 1u && world_clock._ticks == 100u);
    CHECK(s, dok_events_dispatch(&s->scheduler, &report) == DOK_STATUS_OK);
    CHECK(s, report.callbacks == 0u);
}

int dok_time_run_cases(dok_time_test_report *report, dok_event_bank *bank)
{
    test_state state;
    if (report == NULL || bank == NULL) {
        return 1;
    }
    memset(report, 0, sizeof(*report));
    memset(&state, 0, sizeof(state));
    state.report = report;
    state.bank = bank;
    clock_partition(&state);
    clock_extremes(&state);
    clock_pause(&state);
    capacity_lifecycle(&state);
    native_quirks(&state);
    repeat_wrap(&state);
    callback_lifecycle(&state);
    clock_fault(&state);
    mid_dispatch_rollover(&state);
    presentation_separation(&state);
    return report->failures != 0u;
}
