/* Same edge vectors execute on host and ARM. Return failing source line. */
#include "dok/scheduler.h"
#include "dok/clock.h"
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
typedef struct fixture {
    dok_scheduler s;dok_event_owner o[4];uint32_t now, log[32], n, action;
} fixture;
static uint32_t now_fn(void *p) { return ((fixture *)p)->now; }
static void callback(dok_scheduler *s,uint16_t owner,uint16_t id,uint32_t param,void *p)
{
    fixture *f=p;
    if (f->n<32) f->log[f->n++]=owner*100+id+param;
    if (f->action==1) dok_scheduler_cancel(s);
    if (f->action==2 && id==1) {
        dok_scheduler_schedule(s,owner,2,0,0);dok_scheduler_cancel(s);
    }
    if (f->action==3) dok_scheduler_interval(s,77);
    if (f->action==4) dok_scheduler_destroy(s,owner);
    if (f->action==5 && id==1) dok_scheduler_schedule(s,1,2,0,0);
    if (f->action==6) f->now+=5;
    if (f->action==7) dok_scheduler_destroy(s,1);
    if (f->action==8 && id==1) {
        dok_scheduler_schedule(s,1,2,0,0);dok_scheduler_destroy(s,1);
    }
    if (f->action==9 && id==1) dok_scheduler_cancel(s);
}
static void init(fixture *f,uint32_t now)
{
    f->now=now;f->n=0;f->action=0;
    dok_scheduler_init(&f->s,f->o,4,now_fn,f);
    for (uint16_t i=0;i<4;++i) (void)dok_scheduler_owner(&f->s,i);
}
unsigned dok_time_cases(void)
{
    fixture f;init(&f,10);
    CHECK(sizeof(dok_event)==16 && sizeof(dok_event_owner)==56);
    CHECK(dok_scheduler_schedule(&f.s,0,1,9,10));
    CHECK(dok_scheduler_schedule(&f.s,0,2,0,10));
    CHECK(dok_scheduler_schedule(&f.s,0,3,0,10));
    CHECK(!dok_scheduler_schedule(&f.s,0,4,0,10));
    f.now=19;CHECK(dok_scheduler_dispatch(&f.s,callback,&f) && f.n==0);
    f.now=20;CHECK(dok_scheduler_dispatch(&f.s,callback,&f));
    CHECK(f.n==3 && f.log[0]==10 && f.log[1]==2 && f.log[2]==3);
    CHECK(f.o[0].events[0].deadline==30);
    /* No catch-up: late callback repeats from completion time. */
    f.now=100;f.action=6;dok_scheduler_dispatch(&f.s,callback,&f);
    CHECK(f.n==6 && f.o[0].events[0].deadline==115);
    /* Literal unsigned deadline bug is intentional at rollover. */
    init(&f,UINT32_MAX-2);dok_scheduler_schedule(&f.s,0,1,0,5);
    dok_scheduler_dispatch(&f.s,callback,&f);CHECK(f.n==1);
    init(&f,UINT32_MAX-2);dok_scheduler_schedule(&f.s,0,1,0,1);
    f.now=1;dok_scheduler_dispatch(&f.s,callback,&f);CHECK(f.n==0);
    /* Signed -1 bits: at zero waits until UINT32_MAX; at ten already due. */
    init(&f,0);dok_scheduler_schedule(&f.s,0,1,0,UINT32_MAX);
    dok_scheduler_dispatch(&f.s,callback,&f);CHECK(f.n==0);
    f.now=UINT32_MAX;dok_scheduler_dispatch(&f.s,callback,&f);CHECK(f.n==1);
    init(&f,10);dok_scheduler_schedule(&f.s,0,1,0,UINT32_MAX);
    dok_scheduler_dispatch(&f.s,callback,&f);CHECK(f.n==1);
    /* Ring insertion: last activated first, followed by oldest. */
    init(&f,0);
    for (uint16_t i=0;i<3;++i) dok_scheduler_schedule(&f.s,i,1,0,0);
    dok_scheduler_dispatch(&f.s,callback,&f);
    CHECK(f.n==3 && f.log[0]==201 && f.log[1]==1 && f.log[2]==101);
    /* Native shrinking loop bound skips a surviving owner until next call. */
    f.n=0;f.action=1;dok_scheduler_dispatch(&f.s,callback,&f);
    CHECK(f.n==2 && f.s.owner_count==1);
    dok_scheduler_dispatch(&f.s,callback,&f);CHECK(f.n==3 && !f.s.owner_count);
    /* Appended event visited this sweep; stable compaction. */
    init(&f,0);f.action=2;dok_scheduler_schedule(&f.s,0,1,0,0);
    dok_scheduler_dispatch(&f.s,callback,&f);
    CHECK(f.n==2 && f.log[1]==2 && f.o[0].count==1 && f.o[0].events[0].callback==2);
    /* Helper changes first interval even from second callback. */
    init(&f,0);dok_scheduler_schedule(&f.s,0,1,0,100);
    dok_scheduler_schedule(&f.s,0,2,0,0);f.action=3;
    dok_scheduler_dispatch(&f.s,callback,&f);
    CHECK(f.o[0].events[0].interval==77 && f.o[0].events[0].deadline==100);
    CHECK(f.o[0].events[1].interval==0 && f.o[0].events[1].deadline==0);
    /* Native compacts before callback, then reschedules the source slot. */
    init(&f,0);dok_scheduler_schedule(&f.s,0,1,0,0);
    dok_scheduler_schedule(&f.s,0,2,0,10);f.now=10;f.action=9;
    dok_scheduler_dispatch(&f.s,callback,&f);
    CHECK(f.o[0].count==1 && f.o[0].events[0].callback==2);
    CHECK(f.o[0].events[0].deadline==10 && f.o[0].events[1].deadline==20);
    /* Safe lifecycle corrections: current destruction and deferred activation. */
    init(&f,0);f.action=4;dok_scheduler_schedule(&f.s,0,1,0,0);
    dok_scheduler_dispatch(&f.s,callback,&f);
    CHECK(!f.o[0].alive && !f.o[0].count && !f.s.owner_count);
    CHECK(dok_scheduler_owner(&f.s,0));
    init(&f,0);f.action=5;dok_scheduler_schedule(&f.s,0,1,0,0);
    dok_scheduler_dispatch(&f.s,callback,&f);CHECK(f.n==1 && f.s.owner_count==2);
    f.action=0;dok_scheduler_dispatch(&f.s,callback,&f);CHECK(f.log[1]==102);
    init(&f,0);f.action=8;dok_scheduler_schedule(&f.s,0,1,0,0);
    dok_scheduler_dispatch(&f.s,callback,&f);CHECK(f.n==1 && !f.o[1].alive);
    init(&f,0);dok_scheduler_schedule(&f.s,1,1,0,0);
    dok_scheduler_schedule(&f.s,0,1,0,0);f.action=7;
    dok_scheduler_dispatch(&f.s,callback,&f);CHECK(f.n==1 && f.s.owner_count==1);
    dok_scheduler_clear(&f.s);CHECK(!f.s.owner_count && !f.o[0].count);
    CHECK(!dok_scheduler_cancel(&f.s) && !dok_scheduler_interval(&f.s,1));
    CHECK(!dok_scheduler_schedule(&f.s,4,1,0,0));
    /* Exact rational clock, split batches, hardware and native wrap. */
    dok_clock c,a;dok_clock_init(&c,0,0);dok_clock_init(&a,0,0);
    CHECK(dok_clock_advance(&c,262144)==10026 && c.remainder==23068672);
    for (uint32_t i=1;i<=262144;++i) dok_clock_advance(&a,i);
    CHECK(a.ticks==c.ticks && a.remainder==c.remainder);
    dok_clock_init(&a,UINT32_MAX-15,UINT32_MAX);
    dok_clock_advance(&a,48);CHECK(a.ticks==1 && a.remainder==13973376);
    dok_steps steps;CHECK(!dok_steps_init(&steps,0,0));
    CHECK(dok_steps_init(&steps,UINT32_MAX-49,100));
    dok_steps_sample(&steps,250);CHECK(dok_steps_next(&steps) && steps.now==50);
    CHECK(dok_steps_next(&steps) && steps.now==150);
    CHECK(dok_steps_next(&steps) && steps.now==250 && !dok_steps_next(&steps));
    /* Render cadence changes batching, never the fixed callback sequence. */
    uint32_t totals[2];
    for (unsigned mode=0;mode<2;++mode) {
        init(&f,0);dok_scheduler_schedule(&f.s,0,1,0,100);
        CHECK(dok_steps_init(&steps,0,100));
        for (uint32_t elapsed=mode?1000:100;elapsed<=10000;elapsed+=mode?1000:100) {
            dok_steps_sample(&steps,elapsed);
            while (dok_steps_next(&steps)) {
                f.now=steps.now;dok_scheduler_dispatch(&f.s,callback,&f);
            }
        }
        totals[mode]=f.o[0].events[0].deadline;
        CHECK(totals[mode]==10100 && steps.now==10000 && !steps.debt);
    }
    CHECK(totals[0]==totals[1]);
    return 0;
}
