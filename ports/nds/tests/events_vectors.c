/* N08 independent original-vector adapter. No DOS pointers/layout overlays.
 * stdin is a deliberately small integer protocol produced by check_time.py. */
#include "dok/events.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

enum { MAX_CALLBACK = 64, MAX_ACTIONS = 16 };
typedef struct action { uint32_t opcode, a, b, c, d; } action;
typedef struct script {
    uint32_t advance, once, invoked, count;
    action actions[MAX_ACTIONS];
} script;
static dok_event_bank bank;
static dok_event_scheduler queue;
static dok_object_ref owners[4];
static script scripts[MAX_CALLBACK];
static uint32_t now, epoch = 1u;
static int callback_failure;

static dok_status read_clock(void *context, uint32_t *output)
{
    (void)context;
    printf("R %" PRIu32 "\n", now);
    *output = now;
    return DOK_STATUS_OK;
}

static dok_object_ref reference(uint32_t owner)
{
    dok_object_ref empty = {0};
    return owner >= 1u && owner <= 4u ? owners[owner - 1u] : empty;
}

static void callback(void *context, dok_event_scheduler *scheduler,
                     dok_object_ref owner, uint16_t id, uint32_t parameter)
{
    script *item;
    uint32_t i;
    (void)context;
    (void)parameter;
    printf("C %" PRIu32 " %u %" PRIu32 "\n", owner.object_id, (unsigned)id, now);
    if (id >= MAX_CALLBACK) { callback_failure = 1; return; }
    item = &scripts[id];
    if (item->once && item->invoked) return;
    item->invoked = 1u;
    now += item->advance;
    for (i = 0u; i < item->count; ++i) {
        const action *a = &item->actions[i];
        dok_status status = DOK_STATUS_INVALID_ARGUMENT;
        switch (a->opcode) {
        case 1u: status = dok_events_cancel_current(scheduler); break;
        case 2u: status = dok_events_set_interval(scheduler, a->a); break;
        case 3u: status = dok_events_schedule(scheduler, reference(a->a),
                         (uint16_t)a->b, a->d, a->c); break;
        case 4u: status = dok_events_destroy(scheduler, reference(a->a)); break;
        default: callback_failure = 1; break;
        }
        if (status != DOK_STATUS_OK) callback_failure = 1;
    }
}

static int register_owners(void)
{
    uint32_t i;
    for (i = 0u; i < 4u; ++i) {
        owners[i].world_epoch = epoch;
        owners[i].object_id = i + 1u;
        owners[i].generation = 1u;
        if (dok_events_register(&queue, owners[i], 4u) != DOK_STATUS_OK) return 0;
    }
    return 1;
}

int main(void)
{
    char command[8];
    dok_event_config config;
    uint32_t a, b, c, d, e, i;
    memset(&config, 0, sizeof(config));
    config.read_ticks = read_clock;
    config.callback = callback;
    config.world_epoch = epoch;
    if (dok_events_init(&queue, &bank, &config) != DOK_STATUS_OK || !register_owners()) return 2;
    while (scanf("%7s", command) == 1) {
        if (!strcmp(command, "A")) {
            if (scanf("%" SCNu32 " %" SCNu32 " %" SCNu32, &a, &b, &c) != 3 || a >= MAX_CALLBACK) return 2;
            scripts[a].advance = b; scripts[a].once = c;
        } else if (!strcmp(command, "B")) {
            script *item;
            if (scanf("%" SCNu32 " %" SCNu32 " %" SCNu32 " %" SCNu32 " %" SCNu32 " %" SCNu32,
                       &a, &b, &c, &d, &e, &i) != 6 || a >= MAX_CALLBACK) return 2;
            item = &scripts[a];
            if (item->count == MAX_ACTIONS) return 2;
            item->actions[item->count++] = (action){b,c,d,e,i};
        } else if (!strcmp(command, "N")) {
            if (scanf("%" SCNu32, &now) != 1) return 2;
        } else if (!strcmp(command, "S")) {
            dok_status status;
            if (scanf("%" SCNu32 " %" SCNu32 " %" SCNu32 " %" SCNu32, &a, &b, &c, &d) != 4 || b > UINT16_MAX) return 2;
            status = dok_events_schedule(&queue, reference(a), (uint16_t)b, d, c);
            /* Original silently discards capacity overflow; the typed port
             * returns a status while preserving all active queue bytes. */
            if (status != DOK_STATUS_OK && status != DOK_STATUS_NO_SPACE) return 3;
        } else if (!strcmp(command, "D")) {
            dok_event_dispatch_report report;
            if (dok_events_dispatch(&queue, &report) != DOK_STATUS_OK) return 3;
        } else if (!strcmp(command, "K")) {
            if (dok_events_clear(&queue) != DOK_STATUS_OK) return 3;
        } else if (!strcmp(command, "X")) {
            if (scanf("%" SCNu32, &a) != 1 || dok_events_destroy(&queue, reference(a)) != DOK_STATUS_OK) return 3;
        } else if (!strcmp(command, "END")) break;
        else return 2;
    }
    if (callback_failure) return 3;
    a = queue._owner_count ? bank.owners[queue._cursor].reference.object_id : 0u;
    printf("H %" PRIu32 " %" PRIu32 " %" PRIu32 "\n", a, queue._owner_count, queue._event_count);
    for (i = 0u; i < 4u; ++i) {
        const dok_event_owner *owner;
        dok_status status = dok_events_inspect_owner(&queue, owners[i], &owner);
        uint32_t j, count = status == DOK_STATUS_OK ? owner->count : 0u;
        printf("O %" PRIu32 " %" PRIu32 "\n", i + 1u, count);
        for (j = 0u; j < count; ++j) {
            const dok_event_record *event = &owner->events[j];
            printf("E %" PRIu32 " %" PRIu32 " %u %" PRIu32 "\n",
                   event->deadline, event->interval, (unsigned)event->callback, event->parameter);
        }
    }
    return 0;
}
