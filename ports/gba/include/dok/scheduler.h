#ifndef DOK_SCHEDULER_H
#define DOK_SCHEDULER_H
#include <stdint.h>
#define DOK_EVENT_SLOTS 3
#define DOK_OWNER_NONE UINT16_MAX
typedef struct dok_event {
    uint32_t deadline, interval, parameter;
    uint16_t callback, reserved;
} dok_event;
typedef struct dok_event_owner {
    dok_event events[DOK_EVENT_SLOTS];
    uint16_t count, next, previous, alive;
} dok_event_owner;
typedef uint32_t (*dok_now_fn)(void *context);
typedef struct dok_scheduler {
    dok_event_owner *owners;
    uint16_t capacity, cursor, owner_count, current;
    uint16_t dispatching, keep, deleted, pending_first, pending_last;
    dok_now_fn now;
    void *clock_context;
} dok_scheduler;
typedef void (*dok_event_fn)(dok_scheduler *, uint16_t owner,
                             uint16_t callback, uint32_t parameter, void *context);
void dok_scheduler_init(dok_scheduler *, dok_event_owner *, uint16_t capacity,
                        dok_now_fn, void *clock_context);
int dok_scheduler_owner(dok_scheduler *, uint16_t owner);
void dok_scheduler_destroy(dok_scheduler *, uint16_t owner);
int dok_scheduler_schedule(dok_scheduler *, uint16_t owner, uint16_t callback,
                            uint32_t parameter, uint32_t interval_bits);
/* Native helpers address the first slot, not the current compacted slot. */
int dok_scheduler_interval(dok_scheduler *, uint32_t interval_bits);
int dok_scheduler_cancel(dok_scheduler *);
/* One native dispatch(0) sweep; no busy wait or implicit catch-up. */
int dok_scheduler_dispatch(dok_scheduler *, dok_event_fn, void *context);
void dok_scheduler_clear(dok_scheduler *);
#endif
