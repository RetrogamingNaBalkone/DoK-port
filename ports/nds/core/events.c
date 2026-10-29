#include "dok/events.h"

#include <stddef.h>
#include <string.h>

#define EVENTS_MAGIC UINT32_C(0x4E303845)
#define NONE UINT16_MAX
#define REGISTERED 1u
#define ACTIVE 2u
#define PENDING 4u
#define DESTROY 8u

static dok_status valid(const dok_event_scheduler *s)
{
    if (s == NULL) {
        return DOK_STATUS_INVALID_ARGUMENT;
    }
    if (s->_initialized != EVENTS_MAGIC || s->_fault != 0u) {
        return DOK_STATUS_INVALID_STATE;
    }
    return DOK_STATUS_OK;
}

static dok_status find_owner(const dok_event_scheduler *s, dok_object_ref ref,
                              uint16_t *out)
{
    uint16_t i;
    if (dok_object_ref_is_valid(ref) == 0u) {
        return DOK_STATUS_INVALID_ARGUMENT;
    }
    if (ref.world_epoch != s->_config.world_epoch) {
        return DOK_STATUS_STALE_REFERENCE;
    }
    for (i = 0u; i < DOK_EVENT_OWNER_LIMIT; ++i) {
        const dok_event_owner *owner = &s->_bank->owners[i];
        if ((owner->_flags & REGISTERED) != 0u &&
            owner->reference.object_id == ref.object_id) {
            if (dok_object_ref_equal(owner->reference, ref) == 0u ||
                (owner->_flags & DESTROY) != 0u) {
                return DOK_STATUS_STALE_REFERENCE;
            }
            *out = i;
            return DOK_STATUS_OK;
        }
    }
    return DOK_STATUS_STALE_REFERENCE;
}

static void activate(dok_event_scheduler *s, uint16_t index)
{
    dok_event_owner *owner = &s->_bank->owners[index];
    if (s->_owner_count == 0u) {
        owner->_next = index;
        owner->_previous = index;
    } else {
        dok_event_owner *cursor = &s->_bank->owners[s->_cursor];
        owner->_next = cursor->_next;
        owner->_previous = s->_cursor;
        s->_bank->owners[cursor->_next]._previous = index;
        cursor->_next = index;
    }
    owner->_flags |= ACTIVE;
    s->_cursor = index;
    ++s->_owner_count;
}

static void unlink_owner(dok_event_scheduler *s, uint16_t index)
{
    dok_event_owner *owner = &s->_bank->owners[index];
    if (s->_owner_count == 1u) {
        s->_cursor = NONE;
    } else {
        s->_bank->owners[owner->_next]._previous = owner->_previous;
        s->_bank->owners[owner->_previous]._next = owner->_next;
        if (s->_cursor == index) {
            s->_cursor = owner->_next;
        }
    }
    owner->_flags &= (uint16_t)~ACTIVE;
    --s->_owner_count;
}

static void pend(dok_event_scheduler *s, uint16_t index)
{
    dok_event_owner *owner = &s->_bank->owners[index];
    owner->_flags |= PENDING;
    owner->_pending_next = NONE;
    if (s->_pending_tail == NONE) {
        s->_pending_head = index;
    } else {
        s->_bank->owners[s->_pending_tail]._pending_next = index;
    }
    s->_pending_tail = index;
}

static void publish_pending(dok_event_scheduler *s)
{
    uint16_t current = s->_pending_head;
    while (current != NONE) {
        dok_event_owner *owner = &s->_bank->owners[current];
        uint16_t next = owner->_pending_next;
        if ((owner->_flags & DESTROY) != 0u) {
            memset(owner, 0, sizeof(*owner));
        } else {
            owner->_flags &= (uint16_t)~PENDING;
            owner->_pending_next = NONE;
            activate(s, current);
        }
        current = next;
    }
    s->_pending_head = NONE;
    s->_pending_tail = NONE;
}

dok_status dok_events_init(dok_event_scheduler *s, dok_event_bank *bank,
                            const dok_event_config *config)
{
    dok_event_config copy;
    if (s == NULL || bank == NULL || config == NULL ||
        config->read_ticks == NULL || config->callback == NULL ||
        config->world_epoch == 0u) {
        return DOK_STATUS_INVALID_ARGUMENT;
    }
    copy = *config;
    memset(bank, 0, sizeof(*bank));
    memset(s, 0, sizeof(*s));
    s->_bank = bank;
    s->_config = copy;
    s->_cursor = NONE;
    s->_current = NONE;
    s->_pending_head = NONE;
    s->_pending_tail = NONE;
    s->_initialized = EVENTS_MAGIC;
    return DOK_STATUS_OK;
}

dok_status dok_events_register(dok_event_scheduler *s, dok_object_ref ref,
                                uint16_t capacity)
{
    uint16_t i, free_slot = NONE;
    dok_status status = valid(s);
    if (status != DOK_STATUS_OK) {
        return status;
    }
    if (s->_dispatching != 0u) {
        return DOK_STATUS_INVALID_STATE;
    }
    if (dok_object_ref_is_valid(ref) == 0u || capacity > 4u) {
        return DOK_STATUS_INVALID_ARGUMENT;
    }
    if (ref.world_epoch != s->_config.world_epoch) {
        return DOK_STATUS_STALE_REFERENCE;
    }
    for (i = 0u; i < DOK_EVENT_OWNER_LIMIT; ++i) {
        dok_event_owner *owner = &s->_bank->owners[i];
        if (owner->_flags == 0u && free_slot == NONE) {
            free_slot = i;
        }
        if ((owner->_flags & REGISTERED) != 0u &&
            owner->reference.object_id == ref.object_id) {
            return DOK_STATUS_INVALID_STATE;
        }
    }
    if (free_slot == NONE) {
        return DOK_STATUS_NO_SPACE;
    }
    s->_bank->owners[free_slot].reference = ref;
    s->_bank->owners[free_slot].capacity = capacity;
    s->_bank->owners[free_slot]._flags = REGISTERED;
    s->_bank->owners[free_slot]._next = NONE;
    s->_bank->owners[free_slot]._previous = NONE;
    s->_bank->owners[free_slot]._pending_next = NONE;
    ++s->_registered_count;
    return DOK_STATUS_OK;
}

dok_status dok_events_destroy(dok_event_scheduler *s, dok_object_ref ref)
{
    uint16_t index;
    dok_event_owner *owner;
    dok_status status = valid(s);
    if (status != DOK_STATUS_OK) {
        return status;
    }
    status = find_owner(s, ref, &index);
    if (status != DOK_STATUS_OK) {
        return status;
    }
    owner = &s->_bank->owners[index];
    if (s->_dispatching != 0u && index == s->_current) {
        owner->_flags |= DESTROY;
        return DOK_STATUS_OK;
    }
    s->_event_count -= owner->count;
    --s->_registered_count;
    if ((owner->_flags & ACTIVE) != 0u) {
        unlink_owner(s, index);
    }
    if ((owner->_flags & PENDING) != 0u) {
        owner->_flags = PENDING | DESTROY;
        owner->count = 0u;
    } else {
        memset(owner, 0, sizeof(*owner));
    }
    return DOK_STATUS_OK;
}

dok_status dok_events_schedule(dok_event_scheduler *s, dok_object_ref ref,
                                uint16_t callback, uint32_t parameter,
                                uint32_t interval)
{
    dok_status status = valid(s);
    uint16_t index;
    uint32_t now;
    dok_event_owner *owner;
    dok_event_record event;
    if (status != DOK_STATUS_OK) {
        return status;
    }
    if (callback == 0u) {
        return DOK_STATUS_INVALID_ARGUMENT;
    }
    status = find_owner(s, ref, &index);
    if (status != DOK_STATUS_OK) {
        return status;
    }
    owner = &s->_bank->owners[index];
    if (owner->capacity < 2u || owner->count >= owner->capacity - 1u) {
        return DOK_STATUS_NO_SPACE;
    }
    status = s->_config.read_ticks(s->_config.clock_context, &now);
    if (status != DOK_STATUS_OK) {
        s->_fault = 1u;
        return status;
    }
    event.deadline = now + interval;
    event.interval = interval;
    event.parameter = parameter;
    event.callback = callback;
    event._reserved = 0u;
    owner->events[owner->count] = event;
    if (owner->count == 0u) {
        if (s->_dispatching != 0u) {
            pend(s, index);
        } else {
            activate(s, index);
        }
    }
    ++owner->count;
    ++s->_event_count;
    return DOK_STATUS_OK;
}

dok_status dok_events_cancel_current(dok_event_scheduler *s)
{
    dok_status status = valid(s);
    if (status != DOK_STATUS_OK) {
        return status;
    }
    if (s->_in_callback == 0u) {
        return DOK_STATUS_INVALID_STATE;
    }
    s->_keep = 0u;
    return DOK_STATUS_OK;
}

dok_status dok_events_set_interval(dok_event_scheduler *s, uint32_t interval)
{
    dok_status status = valid(s);
    if (status != DOK_STATUS_OK) {
        return status;
    }
    if (s->_in_callback == 0u) {
        return DOK_STATUS_INVALID_STATE;
    }
    s->_bank->owners[s->_current].events[0].interval = interval;
    return DOK_STATUS_OK;
}

static dok_status read_time(dok_event_scheduler *s,
                            dok_event_dispatch_report *report, uint32_t *out)
{
    dok_status status;
    ++report->clock_reads;
    status = s->_config.read_ticks(s->_config.clock_context, out);
    if (status != DOK_STATUS_OK) {
        s->_fault = 1u;
    }
    return status;
}

dok_status dok_events_dispatch(dok_event_scheduler *s,
                                dok_event_dispatch_report *out_report)
{
    dok_event_dispatch_report report = {0u, 0u, 0u, 0u};
    dok_status status = valid(s);
    uint32_t now;
    if (status != DOK_STATUS_OK) {
        return status;
    }
    if (out_report == NULL) {
        return DOK_STATUS_INVALID_ARGUMENT;
    }
    if (s->_dispatching != 0u) {
        return DOK_STATUS_INVALID_STATE;
    }
    if (s->_event_count == 0u) {
        *out_report = report;
        return DOK_STATUS_OK;
    }
    status = read_time(s, &report, &now); /* Native end = now + 0. */
    if (status != DOK_STATUS_OK) {
        *out_report = report;
        return status;
    }
    s->_dispatching = 1u;
    while (report.owners_visited < s->_owner_count &&
           status == DOK_STATUS_OK) {
        uint16_t index = s->_cursor;
        dok_event_owner *owner = &s->_bank->owners[index];
        uint16_t read = 0u, write = 0u;
        uint16_t next;
        s->_current = index;
        while (read < owner->count) {
            owner->events[write] = owner->events[read];
            s->_keep = 1u;
            ++report.slots_examined;
            status = read_time(s, &report, &now);
            if (status == DOK_STATUS_OK && now >= owner->events[read].deadline) {
                dok_event_record event = owner->events[read];
                s->_in_callback = 1u;
                ++report.callbacks;
                s->_config.callback(s->_config.callback_context, s,
                                    owner->reference, event.callback,
                                    event.parameter);
                s->_in_callback = 0u;
                if (s->_fault != 0u) {
                    status = DOK_STATUS_INVALID_STATE;
                } else if ((owner->_flags & DESTROY) == 0u) {
                    status = read_time(s, &report, &now);
                    if (status == DOK_STATUS_OK) {
                        owner->events[read].deadline = now +
                            owner->events[read].interval;
                    }
                }
            }
            if ((owner->_flags & DESTROY) != 0u) {
                break;
            }
            if (s->_keep != 0u) {
                ++write;
            }
            ++read;
            if (status != DOK_STATUS_OK) {
                /* Preserve unread slots without callbacks after clock failure.
                 * Already delivered effects cannot be transactionally undone. */
                while (read < owner->count) {
                    owner->events[write++] = owner->events[read++];
                }
                break;
            }
        }
        next = owner->_next;
        if ((owner->_flags & DESTROY) != 0u) {
            s->_event_count -= owner->count;
            --s->_registered_count;
            unlink_owner(s, index);
            memset(owner, 0, sizeof(*owner));
        } else {
            s->_event_count -= (uint32_t)(owner->count - write);
            owner->count = write;
            if (write == 0u) {
                unlink_owner(s, index);
            }
        }
        if (s->_owner_count != 0u) {
            s->_cursor = next;
        }
        ++report.owners_visited;
    }
    s->_current = NONE;
    if (status == DOK_STATUS_OK) {
        status = read_time(s, &report, &now); /* Native final end test. */
    }
    publish_pending(s);
    s->_in_callback = 0u;
    s->_dispatching = 0u;
    *out_report = report;
    return status;
}

dok_status dok_events_reset(dok_event_scheduler *s, dok_world_epoch epoch)
{
    dok_event_config config;
    if (s == NULL) {
        return DOK_STATUS_INVALID_ARGUMENT;
    }
    if (s->_initialized != EVENTS_MAGIC || s->_dispatching != 0u) {
        return DOK_STATUS_INVALID_STATE;
    }
    if (epoch == 0u || epoch <= s->_config.world_epoch) {
        return DOK_STATUS_LIMIT_REACHED;
    }
    config = s->_config;
    config.world_epoch = epoch;
    return dok_events_init(s, s->_bank, &config);
}

dok_status dok_events_clear(dok_event_scheduler *s)
{
    uint16_t i;
    dok_status status = valid(s);
    if (status != DOK_STATUS_OK) {
        return status;
    }
    if (s->_dispatching != 0u) {
        return DOK_STATUS_INVALID_STATE;
    }
    for (i = 0u; i < DOK_EVENT_OWNER_LIMIT; ++i) {
        dok_event_owner *owner = &s->_bank->owners[i];
        owner->count = 0u;
        owner->_flags &= REGISTERED;
        owner->_next = NONE;
        owner->_previous = NONE;
        owner->_pending_next = NONE;
    }
    s->_event_count = 0u;
    s->_owner_count = 0u;
    s->_cursor = NONE;
    return DOK_STATUS_OK;
}

dok_status dok_events_inspect_owner(const dok_event_scheduler *s,
                                     dok_object_ref ref,
                                     const dok_event_owner **out_owner)
{
    uint16_t index;
    dok_status status;
    if (s == NULL || out_owner == NULL) {
        return DOK_STATUS_INVALID_ARGUMENT;
    }
    if (s->_initialized != EVENTS_MAGIC) {
        return DOK_STATUS_INVALID_STATE;
    }
    status = find_owner(s, ref, &index);
    if (status == DOK_STATUS_OK) {
        *out_owner = &s->_bank->owners[index];
    }
    return status;
}
