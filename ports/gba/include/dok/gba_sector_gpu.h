#ifndef DOK_GBA_SECTOR_GPU_H
#define DOK_GBA_SECTOR_GPU_H
#include <stdint.h>
typedef struct {
    const uint8_t *source;
    uint16_t face,sx,sy;
    uint8_t width,height,source_width,shade,black,reserved;
} dok_sector_gpu_patch;
typedef struct {
    uint32_t valid,failed,count,matrices,bytes,view;
    uint16_t oam[128][4];
    int16_t matrix[32][2];
    uint16_t scan[144];
    uint8_t texture[16384];
    uint16_t ui_cost[144];
    dok_sector_gpu_patch patch[64];
    uint8_t clip_top[120],clip_bottom[120],deny_first[120],deny_last[120];
#ifdef DOK_RENDER_SECTOR_GPU_SHEAR_PROTOTYPE
    int16_t pc[32];
#endif
} dok_sector_gpu_state;
extern dok_sector_gpu_state *g_sector_gpu;
extern volatile uint32_t g_sector_gpu_work[12];
extern volatile uint32_t g_sector_gpu_ticks[3];
extern volatile uint32_t g_sector_gpu_trace;
#endif
