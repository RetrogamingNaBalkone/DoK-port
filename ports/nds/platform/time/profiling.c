/* N23 ARM9 profiling only. Keep SDK timer selection, units and uint32 wrap.
 * Its low/high/low/high sampler assumes no long IRQ between register reads.
 * Mask IRQ dispatch only for the sampler, preserving the caller's IME. */
#include <nds.h>

uint32_t __real_cpuGetTiming(void);

uint32_t __wrap_cpuGetTiming(void)
{
    int previous=enterCriticalSection();
    uint32_t sample=__real_cpuGetTiming();
    leaveCriticalSection(previous);
    return sample;
}
