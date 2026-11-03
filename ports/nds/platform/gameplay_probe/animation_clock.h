#ifndef NDS_ANIMATION_CLOCK_H
#define NDS_ANIMATION_CLOCK_H
#include <stdint.h>
/* NDS adaptation: source PIT ticks (1193182/119 Hz), never render frames.
 * Retain the fraction across presentations, including slow frames. */
static inline uint32_t nds_animation_steps(uint32_t elapsed,uint32_t rate,uint32_t *fraction)
{
    uint64_t value=(uint64_t)elapsed*119u*rate+*fraction;
    *fraction=(uint32_t)(value%1193182u);
    return (uint32_t)(value/1193182u);
}
#endif
