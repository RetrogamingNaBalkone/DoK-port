#include "dok/clock.h"
void dok_clock_init(dok_clock *c, uint32_t counter, uint32_t ticks)
{ c->previous=counter;c->ticks=ticks;c->remainder=0; }
uint32_t dok_clock_advance(dok_clock *c, uint32_t counter)
{
    uint32_t delta=counter-c->previous;c->previous=counter;
    /* Chunking avoids uint64 division helpers on ARM7TDMI. */
    while (delta) {
        uint32_t n=delta>1024?1024:delta;
        uint32_t numerator=c->remainder+n*1193182u;
        c->ticks+=numerator/31195136u;
        c->remainder=numerator%31195136u;delta-=n;
    }
    return c->ticks;
}
int dok_steps_init(dok_steps *s, uint32_t now, uint32_t quantum)
{
    if (!quantum || quantum>UINT32_MAX/2) return 0;
    s->sampled=s->now=now;s->debt=0;s->quantum=quantum;return 1;
}
void dok_steps_sample(dok_steps *s, uint32_t now)
{ s->debt+=now-s->sampled;s->sampled=now; }
int dok_steps_next(dok_steps *s)
{
    if (s->debt<s->quantum) return 0;
    s->debt-=s->quantum;s->now+=s->quantum;return 1;
}
