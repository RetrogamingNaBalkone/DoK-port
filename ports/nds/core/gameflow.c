#include "dok/gameflow.h"
#include <limits.h>

uint32_t dok_gameflow_check(int16_t health,int16_t *lives)
{
    if(health<0)return DOK_FLOW_FINISH;
    if(health>0)return DOK_FLOW_ALIVE;
    uint16_t bits=(uint16_t)((uint16_t)*lives-1u);
    *lives=bits<=INT16_MAX?(int16_t)bits:(int16_t)((int32_t)bits-65536);
    return *lives==0?DOK_FLOW_GAMEOVER:DOK_FLOW_RETRY;
}
void dok_gameflow_restore_motion(dok_motion_state *s,const dok_motion_state *spawn)
{
    s->x=spawn->x;s->y=spawn->y;s->z=spawn->z;
    s->heading=spawn->heading;s->angle_parameter=spawn->angle_parameter;
    s->speed=spawn->speed;s->distance=spawn->distance;
}
