#ifndef DOK_N23_PROFILE_H
#define DOK_N23_PROFILE_H
#include <stdint.h>
#ifndef N23_FINE_PROFILE
#define N23_FINE_PROFILE 0
#endif
#if N23_FINE_PROFILE
/* Diagnostic only. Product builds contain no counters or timer calls.
 * Published once after presentation; the observer reads without guest writes. */
extern uint32_t g_nds_n23_profile[8 + 8 * 16 + 128 * 4];
extern uint32_t dok_n23_fine[8];
uint32_t dok_n23_profile_time(void);
#define N23_TIME(name) uint32_t name = dok_n23_profile_time()
#define N23_ADD(index,value) (dok_n23_fine[index] += (value))
#else
#define N23_TIME(name)
#define N23_ADD(index,value) ((void)0)
#endif
#endif
