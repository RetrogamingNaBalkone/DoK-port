/* N08 host numerical adapter; expectations live in Python's arbitrary-precision
 * integer oracle, not in another C implementation of the conversion. */
#include "dok/time.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static uint64_t counter;
static uint64_t read_counter(void *context)
{
    (void)context;
    return counter;
}

int main(void)
{
    dok_world_clock clock;
    dok_platform_services platform;
    uint32_t initial, output;
    char action;
    memset(&platform, 0, sizeof(platform));
    platform.read_counter = read_counter;
    if (scanf("%" SCNu32 " %" SCNu32 " %" SCNu32 " %" SCNu64,
              &platform.counter_hz_numerator, &platform.counter_hz_denominator,
              &initial, &counter) != 4) return 2;
    if (dok_world_clock_init(&clock, &platform, initial) != DOK_STATUS_OK) return 3;
    while (scanf(" %c %" SCNu64, &action, &counter) == 2) {
        dok_status status;
        output = UINT32_C(0xA55AA55A);
        if (action == 'R') status = dok_world_clock_read(&clock, &output);
        else if (action == 'P') status = dok_world_clock_pause(&clock);
        else if (action == 'U') status = dok_world_clock_resume(&clock);
        else return 2;
        printf("%u %" PRIu32 " %" PRIu64 " %" PRIu64 " %" PRIu32 " %" PRIu32 " %" PRIu64 " %" PRIu64 "\n",
               (unsigned)status, output, clock._last_counter, clock._fraction,
               clock._ticks, clock._paused, clock._numerator, clock._denominator);
    }
    return 0;
}
