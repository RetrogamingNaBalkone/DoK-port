/* DS-specific source only; original uint32 deadlines remain in core/time.c. */
#include <nds.h>
#include <stddef.h>
#include "clock.h"

static uint32_t started;
static uint64_t last_sample;
static uint32_t retry_count;

_Static_assert(BUS_CLOCK == 33513982u, "Re-audit N08 if SDK clock changes");

static uint64_t read_counter(void *context)
{
    (void)context;
    uint64_t sample=systemCounterGetTicks();
    /* A timer wrap and its pending IRQ flag may become visible separately.
     * Resample a backwards SDK result, never fabricate/clamp elapsed time.
     * Persistent failures still reach the world's monotonicity guard. */
    for(unsigned attempt=0;sample<last_sample && attempt<8u;++attempt){
        ++retry_count;
        sample=systemCounterGetTicks();
    }
    last_sample=sample;
    return last_sample;
}

uint64_t dok_nds_clock_last_sample(void) { return last_sample; }
uint32_t dok_nds_clock_retry_count(void) { return retry_count; }

dok_status dok_nds_clock_initialize(dok_platform_services *out_services)
{
    dok_platform_services services = {0};
    if (out_services == NULL)
        return DOK_STATUS_INVALID_ARGUMENT;
    if (!started) {
        systemCounterSetup();
        started = 1;
    }
    services.read_counter = read_counter;
    services.counter_hz_numerator = BUS_CLOCK;
    services.counter_hz_denominator = 64u;
    *out_services = services;
    return DOK_STATUS_OK;
}
