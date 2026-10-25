#include "dok/scheduler.h"

void dok_scheduler_init(dok_scheduler *s, dok_event_owner *owners, uint16_t capacity,
                        dok_now_fn now, void *context)
{
    s->owners=owners;s->capacity=capacity;s->cursor=DOK_OWNER_NONE;
    s->owner_count=0;s->current=DOK_OWNER_NONE;s->dispatching=0;
    s->keep=0;s->deleted=0;s->pending_first=s->pending_last=DOK_OWNER_NONE;
    s->now=now;s->clock_context=context;
    for (uint16_t i=0;i<capacity;++i) {
        owners[i].alive=0;owners[i].count=0;
        owners[i].next=owners[i].previous=DOK_OWNER_NONE;
    }
}
int dok_scheduler_owner(dok_scheduler *s, uint16_t id)
{
    if (id>=s->capacity || s->owners[id].alive ||
        (s->dispatching && id==s->current)) return 0;
    s->owners[id].alive=1;s->owners[id].count=0;
    s->owners[id].next=s->owners[id].previous=DOK_OWNER_NONE;
    return 1;
}
static void link_owner(dok_scheduler *s, uint16_t id)
{
    dok_event_owner *o=&s->owners[id];
    if (!s->owner_count) o->next=o->previous=id;
    else {
        uint16_t head=s->cursor, next=s->owners[head].next;
        o->next=next;o->previous=head;
        s->owners[next].previous=id;s->owners[head].next=id;
    }
    s->cursor=id;++s->owner_count;
}
static void unlink_owner(dok_scheduler *s, uint16_t id)
{
    dok_event_owner *o=&s->owners[id];
    if (s->owner_count>1) {
        s->owners[o->next].previous=o->previous;
        s->owners[o->previous].next=o->next;
    }
    if (s->cursor==id) s->cursor=o->next;
    if (--s->owner_count==0) s->cursor=DOK_OWNER_NONE;
    o->next=o->previous=DOK_OWNER_NONE;
}
void dok_scheduler_destroy(dok_scheduler *s, uint16_t id)
{
    if (id>=s->capacity || !s->owners[id].alive) return;
    dok_event_owner *o=&s->owners[id];
    if (o->alive==2 || o->alive==3) {
        /* Pending list retains this node until end of sweep; no reuse yet. */
        o->count=0;o->alive=3;return;
    }
    if (s->dispatching && id==s->current) {
        /* Keep storage and links valid until callback returns. */
        s->deleted=1;o->alive=0;return;
    }
    if (o->count) unlink_owner(s,id);
    o->alive=0;o->count=0;
}
int dok_scheduler_schedule(dok_scheduler *s, uint16_t id, uint16_t callback,
                            uint32_t parameter, uint32_t interval)
{
    if (id>=s->capacity || !s->owners[id].alive || s->owners[id].alive==3 || !callback) return 0;
    dok_event_owner *o=&s->owners[id];
    if (o->count==DOK_EVENT_SLOTS) return 0;
    /* Defer publication of a new active owner until sweep end. */
    if (!o->count && s->dispatching && o->alive!=2) {
        o->alive=2;o->next=DOK_OWNER_NONE;
        if (s->pending_last!=DOK_OWNER_NONE) s->owners[s->pending_last].next=id;
        else s->pending_first=id;
        s->pending_last=id;
    } else if (!o->count && !s->dispatching) link_owner(s,id);
    dok_event *e=&o->events[o->count++];
    e->callback=callback;e->parameter=parameter;e->interval=interval;e->reserved=0;
    e->deadline=s->now(s->clock_context)+interval;
    return 1;
}
int dok_scheduler_interval(dok_scheduler *s, uint32_t interval)
{
    if (!s->dispatching || s->deleted) return 0;
    s->owners[s->current].events[0].interval=interval;return 1;
}
int dok_scheduler_cancel(dok_scheduler *s)
{
    if (!s->dispatching || s->deleted) return 0;
    s->keep=0;return 1;
}
int dok_scheduler_dispatch(dok_scheduler *s, dok_event_fn fn, void *context)
{
    if (s->dispatching || !fn) return 0;
    s->dispatching=1;
    /* Mutable owner_count intentionally preserves native skip after removal. */
    for (uint16_t visit=0;visit<s->owner_count;++visit) {
        uint16_t id=s->cursor, write=0;
        dok_event_owner *o=&s->owners[id];
        s->current=id;s->deleted=0;
        for (uint16_t read=0;read<o->count;++read) {
            o->events[write]=o->events[read];s->keep=1;
            /* Original copies to the compacted slot but executes/reschedules
             * the source slot. A moved survivor retains its old deadline. */
            dok_event *e=&o->events[read];
            if (s->now(s->clock_context)>=e->deadline) {
                fn(s,id,e->callback,e->parameter,context);
                if (s->deleted) break;
                e->deadline=s->now(s->clock_context)+e->interval;
            }
            if (s->keep) ++write;
        }
        uint16_t next=o->next;
        o->count=s->deleted?0:write;
        if (!o->count) unlink_owner(s,id);
        if (s->owner_count) s->cursor=next;
    }
    s->current=DOK_OWNER_NONE;s->dispatching=0;
    uint16_t pending=s->pending_first;
    while (pending!=DOK_OWNER_NONE) {
        dok_event_owner *o=&s->owners[pending];uint16_t next=o->next;
        o->alive=o->count?1:0;
        if (o->count) link_owner(s,pending);
        pending=next;
    }
    s->pending_first=s->pending_last=DOK_OWNER_NONE;return 1;
}
void dok_scheduler_clear(dok_scheduler *s)
{
    if (s->dispatching) return;
    for (uint16_t i=0;i<s->capacity;++i) {
        s->owners[i].count=0;
        s->owners[i].next=s->owners[i].previous=DOK_OWNER_NONE;
    }
    s->cursor=DOK_OWNER_NONE;s->owner_count=0;
}
