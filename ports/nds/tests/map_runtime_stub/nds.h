#ifndef NDS_MAP_RUNTIME_HOST_STUB_H
#define NDS_MAP_RUNTIME_HOST_STUB_H
#include <stdint.h>
#include <stddef.h>
#define KEY_START 8u
#define BUS_CLOCK 33513982u
#define REG_VCOUNT 192u
extern uint16_t test_sub_palette[256];
#define BG_PALETTE_SUB test_sub_palette
uint32_t cpuGetTiming(void);
void DC_FlushRange(void *,size_t);
void dmaCopy(const void *,void *,size_t);
#endif
