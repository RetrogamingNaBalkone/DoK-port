#ifndef DOK_WATER_TEXTURE_H
#define DOK_WATER_TEXTURE_H
#include "dok/effects.h"
/* Two exact logical copies in POT storage; no resampling/hardware wrap. */
dok_status dok_water_texture(const unsigned char *,uint16_t,uint16_t,uint16_t,uint32_t,unsigned char *,uint32_t);
uint32_t dok_water_uv_shift(uint32_t phase,uint32_t size,uint32_t negative);
#endif
