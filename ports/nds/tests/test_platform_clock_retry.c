#include "dok/time.h"
#include "../platform/time/clock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t samples[16];
static unsigned used, count, setups, checks;
#define CHECK(x) do { ++checks; if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
uint64_t systemCounterGetTicks(void) { CHECK(used<count); return samples[used++]; }
void systemCounterSetup(void) { ++setups; }
static void feed(uint64_t value, unsigned repeats)
{ used=0;count=repeats;for(unsigned i=0;i<count;++i)samples[i]=value; }
int main(void)
{
    dok_platform_services p;
    dok_world_clock world, before;
    uint32_t ticks=0;
    CHECK(dok_nds_clock_initialize(NULL)==DOK_STATUS_INVALID_ARGUMENT);
    CHECK(dok_nds_clock_initialize(&p)==DOK_STATUS_OK);
    CHECK(setups==1);
    feed(0x27f2fffdu,1);
    CHECK(dok_world_clock_init(&world,&p,0)==DOK_STATUS_OK);
    /* After prolonged uptime, wrap is momentarily visible before IF. */
    feed(0x27f20000u,2);samples[1]=0x27f30001u;
    CHECK(dok_world_clock_read(&world,&ticks)==DOK_STATUS_OK);
    CHECK(used==2 && world._last_counter==0x27f30001u);
    CHECK(dok_nds_clock_retry_count()==1);
    CHECK(dok_nds_clock_last_sample()==world._last_counter);
    CHECK(ticks==0 && world._fraction!=0);
    CHECK(dok_nds_clock_initialize(&p)==DOK_STATUS_OK && setups==1);
    /* Continuous time, including >32-bit hardware extension and pause. */
    feed(0x227f30001ULL,1);
    CHECK(dok_world_clock_read(&world,&ticks)==DOK_STATUS_OK);
    CHECK(used==1 && world._last_counter==0x227f30001ULL);
    feed(0x227f30001ULL,1);CHECK(dok_world_clock_pause(&world)==DOK_STATUS_OK);
    uint32_t paused=ticks;
    feed(0x227f40000ULL,2);samples[0]=0x227f20000ULL;
    CHECK(dok_world_clock_read(&world,&ticks)==DOK_STATUS_OK && ticks==paused);
    CHECK(dok_nds_clock_retry_count()==2);
    feed(0x227f40001ULL,1);CHECK(dok_world_clock_resume(&world)==DOK_STATUS_OK);
    /* A real/persistent rollback is still fatal; no fabricated clock progress. */
    before=world;ticks=0xdeadbeefu;
    feed(0x227f30000ULL,9);
    CHECK(dok_world_clock_read(&world,&ticks)==DOK_STATUS_INVALID_STATE);
    CHECK(used==9 && dok_nds_clock_retry_count()==10);
    CHECK(ticks==0xdeadbeefu && memcmp(&world,&before,sizeof(world))==0);
    CHECK(dok_nds_clock_last_sample()==0x227f30000ULL);
    printf("{\"passed\":true,\"checks\":%u,\"retries\":%u}\n",checks,dok_nds_clock_retry_count());
    return 0;
}
