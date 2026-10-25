#include "dok/clock.h"
#include "dok/scheduler.h"
extern void gba_clock_start(dok_clock *);
extern uint32_t gba_clock_sample(dok_clock *);
extern unsigned dok_time_cases(void);
/* Public ordered uint32 fields, consumed by verify_time_gba.py. */
volatile struct {
    uint32_t magic, error, ticks, counter, remainder, step_now, debt;
    uint32_t steps, callbacks, polls, max_debt, last_callback;
} g_time_diag;
static dok_clock clock_state;
static dok_steps steps;
static dok_scheduler scheduler;
static dok_event_owner owners[1];
static uint32_t fixed_now(void *p) { return ((dok_steps *)p)->now; }
uint32_t gba_time_ticks(void) { return g_time_diag.ticks; }
static void callback(dok_scheduler *s,uint16_t owner,uint16_t id,uint32_t parameter,void *p)
{
    (void)s;(void)owner;(void)id;(void)parameter;(void)p;
    ++g_time_diag.callbacks;g_time_diag.last_callback=steps.now;
}
unsigned gba_time_start(void)
{
    g_time_diag.error=dok_time_cases();
    if (g_time_diag.error) return g_time_diag.error;
    gba_clock_start(&clock_state);
    (void)dok_steps_init(&steps,0,100);
    dok_scheduler_init(&scheduler,owners,1,fixed_now,&steps);
    (void)dok_scheduler_owner(&scheduler,0);
    (void)dok_scheduler_schedule(&scheduler,0,1,0,100);
    g_time_diag.magic=0x54494d36;return 0;
}
void gba_time_poll(void)
{
    uint32_t now=gba_clock_sample(&clock_state);
    dok_steps_sample(&steps,now);
    if (steps.debt>g_time_diag.max_debt) g_time_diag.max_debt=steps.debt;
    /* Bound work without discarding debt; no dependency on displayed frames. */
    for (unsigned i=0;i<128 && dok_steps_next(&steps);++i) {
        (void)dok_scheduler_dispatch(&scheduler,callback,0);++g_time_diag.steps;
    }
    g_time_diag.ticks=now;g_time_diag.counter=clock_state.previous;
    g_time_diag.remainder=clock_state.remainder;g_time_diag.step_now=steps.now;
    g_time_diag.debt=steps.debt;++g_time_diag.polls;
}
