#ifndef DOK_AUDIO_HOST_NDS_H
#define DOK_AUDIO_HOST_NDS_H
#include <stddef.h>
#include <stdint.h>
static inline void DC_FlushRange(const void *base, size_t bytes)
{ (void)base; (void)bytes; }
static inline uint32_t cpuGetTiming(void)
{ static uint32_t ticks; return ++ticks; }
#endif
