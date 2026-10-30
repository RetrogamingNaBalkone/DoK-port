#ifndef DOK_NDS_MAP_CONTROL_H
#define DOK_NDS_MAP_CONTROL_H
#include <stdint.h>
enum { DOK_MAP_WIDTH=256, DOK_MAP_HEIGHT=160, DOK_MAP_SCREEN_BYTES=49152,
       DOK_MAP_CAPTURE_NONE=0, DOK_MAP_CAPTURE_MAP=1,
       DOK_MAP_CAPTURE_MINUS=2,DOK_MAP_CAPTURE_PLUS=3,DOK_MAP_CAPTURE_CENTER=4 };
typedef struct dok_map_control {
    int32_t center_x_q16,center_z_q16;
    int32_t follow_x_q16,follow_z_q16;
    int16_t last_x,last_y;
    uint16_t width,length;
    uint8_t zoom,follow,capture,held,blocked;
    uint32_t starts,actions,drags,revision;
} dok_map_control;
typedef struct dok_map_touch { int16_t x,y; uint8_t held,paused; } dok_map_touch;
extern const uint8_t dok_map_scales[5];
/* Map X points from east to west, as in DOS. Centers use cell coordinates
 * Q16; the world point under the viewport center survives a zoom unless the
 * prescribed bounds require clamping. Hero coordinates are native cells. */
void dok_map_control_init(dok_map_control *,uint16_t width,uint16_t length,
                         int16_t hero_x,int16_t hero_z);
void dok_map_control_step(dok_map_control *,dok_map_touch,
                         int16_t hero_x,int16_t hero_z);
void dok_map_control_view(const dok_map_control *,int32_t *left,int32_t *top);
#endif
