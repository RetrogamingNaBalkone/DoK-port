#ifndef DOK_NDS_UI_H
#define DOK_NDS_UI_H
#include <stdint.h>
#define DOK_UI_BANK_BYTES 8192u
#define DOK_UI_HUD_BYTES 9728u
typedef struct dok_ui_values {
    int16_t health,charge,max_health,max_charge,lives,score;
    uint16_t keys,weapon; /* weapon: owned slots 1/2 as bits 2/4 */
} dok_ui_values;
typedef void (*dok_ui_glyph_fn)(void *,uint32_t offset,uint32_t col,uint32_t row);
/* Consumes every byte through the first NUL/$ once; CR has no raster, LF
 * advances a row. Words wrap intact when possible; long words split. */
uint32_t dok_ui_paginate(const unsigned char *,uint32_t,uint32_t columns,
    uint32_t rows,uint32_t page,dok_ui_glyph_fn,void *);
uint32_t dok_ui_number(int16_t,unsigned char out[7]);
void dok_ui_hud(unsigned char out[DOK_UI_HUD_BYTES],const unsigned char bank[DOK_UI_BANK_BYTES],
    uint32_t language,const dok_ui_values *,const unsigned char *,uint32_t,uint32_t page);
#endif
