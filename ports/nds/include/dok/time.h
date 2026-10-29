#ifndef DOK_NDS_TIME_H
#define DOK_NDS_TIME_H

#include "dok/contracts.h"

/* Native tick scale is nominal PIT 1193182/119 Hz, not a logic update rate.
 * Native uint32 wrap is intentional. This is native storage, never a wire ABI. */
typedef struct dok_world_clock {
    dok_platform_services _platform;
    uint64_t _last_counter;
    uint64_t _numerator;
    uint64_t _denominator;
    uint64_t _fraction;
    uint32_t _ticks;
    uint32_t _paused;
    uint32_t _initialized;
} dok_world_clock;

/* Caller owns storage and context. Init samples the platform once. The
 * platform counter must remain monotonic uint64 for this service lifetime.
 * Arbitrary nonzero uint32 rational frequencies are supported exactly. */
dok_status dok_world_clock_init(dok_world_clock *clock,
                                 const dok_platform_services *platform,
                                 uint32_t initial_ticks);
/* Suitable as an injected scheduler read callback. Failure preserves output
 * and clock state. A backwards counter is INVALID_STATE, never uint64 wrap. */
dok_status dok_world_clock_read(void *clock, uint32_t *out_ticks);
/* Call outside dispatch/callback. Pause accounts for the last active delta;
 * resume samples a fresh baseline without adding menu time. Idempotent calls
 * do not sample the counter. Fraction is retained across pause/resume. */
dok_status dok_world_clock_pause(dok_world_clock *clock);
dok_status dok_world_clock_resume(dok_world_clock *clock);

#endif
