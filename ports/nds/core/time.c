#include "dok/time.h"

#include <stddef.h>

#define CLOCK_MAGIC UINT32_C(0x4E303843)

static uint64_t gcd64(uint64_t a, uint64_t b)
{
    while (b != 0u) {
        uint64_t next = a % b;
        a = b;
        b = next;
    }
    return a;
}

/* Return the low 32 bits of floor((elapsed*num+fraction)/den), updating
 * fraction exactly. The slow branch is bounded by 64 bits, not elapsed time.
 * den <= UINT32_MAX*119, hence 3*den cannot overflow uint64. */
static uint32_t convert(uint64_t elapsed, uint64_t num, uint64_t den,
                        uint64_t *fraction)
{
    if (elapsed <= (UINT64_MAX - *fraction) / num) {
        uint64_t value = elapsed * num + *fraction;
        uint32_t ticks = (uint32_t)(value / den);
        *fraction = value % den;
        return ticks;
    } else {
        uint32_t quotient = (uint32_t)elapsed * (uint32_t)(num / den);
        uint32_t extra = 0u;
        uint64_t remainder = 0u;
        uint64_t multiplier = num % den;
        unsigned int bit = 64u;
        while (bit != 0u) {
            --bit;
            extra *= 2u;
            remainder *= 2u;
            if (((elapsed >> bit) & 1u) != 0u) {
                remainder += multiplier;
            }
            extra += (uint32_t)(remainder / den);
            remainder %= den;
        }
        remainder += *fraction;
        extra += (uint32_t)(remainder / den);
        *fraction = remainder % den;
        return quotient + extra;
    }
}

dok_status dok_world_clock_init(dok_world_clock *clock,
                                 const dok_platform_services *platform,
                                 uint32_t initial_ticks)
{
    dok_world_clock fresh;
    uint64_t divisor;
    if (clock == NULL || platform == NULL || platform->read_counter == NULL ||
        platform->counter_hz_numerator == 0u ||
        platform->counter_hz_denominator == 0u) {
        return DOK_STATUS_INVALID_ARGUMENT;
    }
    fresh._platform = *platform;
    fresh._numerator = UINT64_C(1193182) * platform->counter_hz_denominator;
    fresh._denominator = UINT64_C(119) * platform->counter_hz_numerator;
    divisor = gcd64(fresh._numerator, fresh._denominator);
    fresh._numerator /= divisor;
    fresh._denominator /= divisor;
    fresh._last_counter = fresh._platform.read_counter(fresh._platform.context);
    fresh._fraction = 0u;
    fresh._ticks = initial_ticks;
    fresh._paused = 0u;
    fresh._initialized = CLOCK_MAGIC;
    *clock = fresh;
    return DOK_STATUS_OK;
}

dok_status dok_world_clock_read(void *storage, uint32_t *out_ticks)
{
    dok_world_clock *clock = storage;
    uint64_t current, fraction;
    uint32_t ticks;
    if (clock == NULL || out_ticks == NULL) {
        return DOK_STATUS_INVALID_ARGUMENT;
    }
    if (clock->_initialized != CLOCK_MAGIC) {
        return DOK_STATUS_INVALID_STATE;
    }
    current = clock->_platform.read_counter(clock->_platform.context);
    if (current < clock->_last_counter) {
        return DOK_STATUS_INVALID_STATE;
    }
    fraction = clock->_fraction;
    ticks = clock->_ticks;
    if (clock->_paused == 0u) {
        ticks += convert(current - clock->_last_counter, clock->_numerator,
                         clock->_denominator, &fraction);
    }
    clock->_last_counter = current;
    clock->_fraction = fraction;
    clock->_ticks = ticks;
    *out_ticks = ticks;
    return DOK_STATUS_OK;
}

dok_status dok_world_clock_pause(dok_world_clock *clock)
{
    uint32_t ignored;
    dok_status result;
    if (clock == NULL) {
        return DOK_STATUS_INVALID_ARGUMENT;
    }
    if (clock->_initialized != CLOCK_MAGIC) {
        return DOK_STATUS_INVALID_STATE;
    }
    if (clock->_paused != 0u) {
        return DOK_STATUS_OK;
    }
    result = dok_world_clock_read(clock, &ignored);
    if (result == DOK_STATUS_OK) {
        clock->_paused = 1u;
    }
    return result;
}

dok_status dok_world_clock_resume(dok_world_clock *clock)
{
    uint32_t ignored;
    dok_status result;
    if (clock == NULL) {
        return DOK_STATUS_INVALID_ARGUMENT;
    }
    if (clock->_initialized != CLOCK_MAGIC) {
        return DOK_STATUS_INVALID_STATE;
    }
    if (clock->_paused == 0u) {
        return DOK_STATUS_OK;
    }
    result = dok_world_clock_read(clock, &ignored);
    if (result == DOK_STATUS_OK) {
        clock->_paused = 0u;
    }
    return result;
}
