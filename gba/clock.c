#include "dok/clock.h"
#define R16(a) (*(volatile uint16_t *)(a))
/* Timers 2/3 reserved for the game clock; timers 0/1 remain renderer probes. */
void gba_clock_start(dok_clock *c)
{
    R16(0x0400010a)=0;R16(0x0400010e)=0;
    R16(0x04000108)=0;R16(0x0400010c)=0;
    dok_clock_init(c,0,0);
    R16(0x0400010e)=0x84;R16(0x0400010a)=0x81;
}
uint32_t gba_clock_sample(dok_clock *c)
{
    uint32_t hi,lo,again;
    do {
        hi=R16(0x0400010c);lo=R16(0x04000108);again=R16(0x0400010c);
    } while (hi!=again);
    return dok_clock_advance(c,(hi<<16)|lo);
}
