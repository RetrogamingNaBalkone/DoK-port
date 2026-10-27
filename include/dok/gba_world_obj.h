#ifndef DOK_GBA_WORLD_OBJ_H
#define DOK_GBA_WORLD_OBJ_H
#include <stdint.h>
#define DOK_WORLD_OBJ_SCRATCH 4096u
#define DOK_WORLD_OBJ_MAGIC 0x574f424au
#define DOK_WORLD_OBJ_SLOTS 32u
typedef struct {
    uint32_t magic,count,matrices,view,stride,scan_max;
    uint16_t oam[128][4];
    const uint8_t *slots[DOK_WORLD_OBJ_SLOTS];
    const uint8_t *uploaded[DOK_WORLD_OBJ_SLOTS];
    uint16_t used[DOK_WORLD_OBJ_SLOTS],scanline[144];
    int16_t matrix[30][4];
    uint8_t first_y[128],last_y[128];
} dok_gba_world_obj_state;
extern dok_gba_world_obj_state *g_world_obj_state;
extern volatile uint32_t g_world_obj_work[12];
#endif
