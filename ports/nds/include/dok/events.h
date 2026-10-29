#ifndef DOK_NDS_EVENTS_H
#define DOK_NDS_EVENTS_H

#include "dok/contracts.h"
#include "dok/memory_budget.h"

enum { DOK_EVENT_OWNER_LIMIT = 512, DOK_EVENT_SLOT_LIMIT = 3 };

typedef dok_status (*dok_event_read_fn)(void *context, uint32_t *out_ticks);
struct dok_event_scheduler;
/* Callback ID replaces a DOS far pointer; parameter/context are explicit port
 * adapter arguments, not the native callback signature (owner only). */
typedef void (*dok_event_callback_fn)(void *context,
                                      struct dok_event_scheduler *scheduler,
                                      dok_object_ref owner, uint16_t callback,
                                      uint32_t parameter);

typedef struct dok_event_record {
    uint32_t deadline;
    uint32_t interval;
    uint32_t parameter;
    uint16_t callback;
    uint16_t _reserved;
} dok_event_record;

typedef struct dok_event_owner {
    dok_object_ref reference;
    dok_event_record events[DOK_EVENT_SLOT_LIMIT];
    uint16_t count;
    uint16_t capacity;
    uint16_t _next;
    uint16_t _previous;
    uint16_t _pending_next;
    uint16_t _flags;
} dok_event_owner;

/* Caller owns typed storage in the events slab. Private fields are exposed
 * solely to permit native C storage without malloc or byte-buffer casts. */
typedef struct dok_event_bank {
    dok_event_owner owners[DOK_EVENT_OWNER_LIMIT];
} dok_event_bank;

typedef struct dok_event_config {
    void *clock_context;
    dok_event_read_fn read_ticks;
    void *callback_context;
    dok_event_callback_fn callback;
    dok_world_epoch world_epoch;
} dok_event_config;

typedef struct dok_event_scheduler {
    dok_event_bank *_bank;
    dok_event_config _config;
    uint32_t _initialized;
    uint32_t _event_count;
    uint32_t _registered_count;
    uint32_t _owner_count;
    uint32_t _fault;
    uint32_t _dispatching;
    uint32_t _in_callback;
    uint32_t _keep;
    uint16_t _cursor;
    uint16_t _current;
    uint16_t _pending_head;
    uint16_t _pending_tail;
} dok_event_scheduler;

typedef struct dok_event_dispatch_report {
    uint32_t owners_visited;
    uint32_t slots_examined;
    uint32_t callbacks;
    /* Dispatch's initial/due/post/final reads; nested schedule performs its
     * own clock read, which is not included in this diagnostic counter. */
    uint32_t clock_reads;
} dok_event_dispatch_report;

_Static_assert(sizeof(dok_event_bank) <= DOK_NDS_EVENTS_BYTES,
               "N08 owner bank must fit N03 events slab");

/* Initialize unused storage; all references must use the configured nonzero
 * epoch. Register/unregister preserve object lifetime identity; reusing the
 * same object identity for a new lifetime violates the N03 caller contract. */
dok_status dok_events_init(dok_event_scheduler *scheduler, dok_event_bank *bank,
                            const dok_event_config *config);
/* Native capacity 0..4; usable slots = max(capacity-1,0). Register is forbidden
 * inside dispatch. Object ID must be unique among registered owners. */
dok_status dok_events_register(dok_event_scheduler *scheduler,
                                dok_object_ref owner, uint16_t native_capacity);
dok_status dok_events_destroy(dok_event_scheduler *scheduler,
                               dok_object_ref owner);
dok_status dok_events_schedule(dok_event_scheduler *scheduler,
                                dok_object_ref owner, uint16_t callback,
                                uint32_t parameter, uint32_t interval);
/* Callback-local helpers retain original global keep / first-slot behavior.
 * Cancel affects the currently executing slot, not the whole owner. */
dok_status dok_events_cancel_current(dok_event_scheduler *scheduler);
dok_status dok_events_set_interval(dok_event_scheduler *scheduler,
                                    uint32_t interval);
/* One native dispatch(0) sweep. Does not busy-wait or catch up missed periods.
 * Mid-dispatch uint32 clock rollover never re-enters the original end-test
 * busy loop; the native unsigned per-event deadline comparison is retained.
 * Empty queue reads no clock. A clock failure latches fault; reset is required
 * before another dispatch, and already completed callbacks are not rolled back.
 * Output is written for a started sweep, including a partial failed sweep. */
dok_status dok_events_dispatch(dok_event_scheduler *scheduler,
                                dok_event_dispatch_report *out_report);
/* Native clear-all semantics: retain registered owners and capacity/identity,
 * remove every event. Outside dispatch only; does not clear a latched fault. */
dok_status dok_events_clear(dok_event_scheduler *scheduler);
/* Clears events and registrations, installs a new nonzero epoch. Cannot run
 * during dispatch. Clock/callback descriptors are retained. */
dok_status dok_events_reset(dok_event_scheduler *scheduler,
                             dok_world_epoch new_epoch);
/* Borrowed const storage, valid until the next mutation. Output unchanged on
 * failure. The native slot bytes beyond count are not active event state. */
dok_status dok_events_inspect_owner(const dok_event_scheduler *scheduler,
                                     dok_object_ref owner,
                                     const dok_event_owner **out_owner);

#endif
