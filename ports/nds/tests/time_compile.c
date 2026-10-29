#include "dok/time.h"
#include "dok/events.h"
#include "time_probe.h"

_Static_assert(DOK_EVENT_OWNER_LIMIT == 512, "accepted owner capacity");
_Static_assert(sizeof(dok_event_bank) <= DOK_NDS_EVENTS_BYTES, "events slab capacity");
_Static_assert(sizeof(NdsTimeTraceRecord) == 29u * sizeof(uint32_t), "trace ABI scalar size");

int dok_time_header_compile(void)
{
    return (int)(sizeof(dok_world_clock) + sizeof(dok_event_scheduler));
}
