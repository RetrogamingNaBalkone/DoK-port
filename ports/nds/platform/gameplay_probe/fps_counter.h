#ifndef NDS_FPS_COUNTER_H
#define NDS_FPS_COUNTER_H
#include <stdint.h>
typedef struct NdsFpsCounter {
    uint32_t epoch,last,ticks,frames,tenths;
} NdsFpsCounter;
static inline void nds_fps_present(NdsFpsCounter *f,uint32_t epoch,uint32_t now)
{
    if(f->epoch!=epoch){*f=(NdsFpsCounter){epoch,now,0,0,0};return;}
    f->ticks+=now-f->last;f->last=now;++f->frames;
    if(f->ticks>=10027u){
        f->tenths=(uint32_t)(((uint64_t)f->frames*11931820u+(uint64_t)f->ticks*119u/2u)/((uint64_t)f->ticks*119u));
        if(f->tenths>999u)f->tenths=999u;
        f->ticks=f->frames=0;
    }
}
#endif
