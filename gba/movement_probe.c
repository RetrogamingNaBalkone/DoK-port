#include "dok/movement.h"
#include "dok/clock.h"
extern uint32_t gba_time_ticks(void);
extern unsigned dok_movement_cases(void);
/* Commit request last. test_mode freezes live movement; count advances exact
 * 416-tick updates. Mailbox is test-only, not an alternative game control. */
volatile struct {uint32_t test_mode,request,ack,commands,count;} g_motion_probe;
volatile struct {
    uint32_t magic,error,steps,debt,commands,last_cycles,max_cycles,total_cycles;
    int32_t x,y,z,heading,direction,step,accumulator_a,accumulator_b,command;
    uint32_t held,pressed,released,seen,quantum;
    int32_t step_distance;
    uint32_t motion_flags,remembered_flags;
} g_motion_diag;
static dok_motion motion;
static dok_input input;
static dok_steps steps;
static uint32_t pending_pressed;
static dok_runtime_actor *player;

static dok_runtime_actor *player_actor(dok_runtime *world)
{
    if(!world->player_index || !world->level) return 0;
    const dok_runtime_definition *def=dok_runtime_definition_at(world->level,world->player_index-1);
    if(!def || def->class_id!=1 || def->actor_index>=world->actor_count) return 0;
    return &world->actors[def->actor_index];
}
static void snapshot(void)
{
    g_motion_diag.x=player->x;g_motion_diag.y=player->y;g_motion_diag.z=player->z;
    g_motion_diag.heading=player->heading;g_motion_diag.direction=player->direction;
    g_motion_diag.step=player->step;g_motion_diag.accumulator_a=player->accumulator_a;
    g_motion_diag.accumulator_b=player->accumulator_b;g_motion_diag.command=player->command;
    g_motion_diag.step_distance=player->step_distance;g_motion_diag.motion_flags=player->motion_flags;
    g_motion_diag.remembered_flags=motion.remembered_flags;
}
unsigned gba_motion_start(dok_runtime *world)
{
    g_motion_diag.error=dok_movement_cases();
    if(g_motion_diag.error) return g_motion_diag.error;
    player=player_actor(world);if(!player) return 1;
    motion.remembered_flags=0;input=(dok_input){0};pending_pressed=0;
    dok_steps_init(&steps,gba_time_ticks(),416);
    g_motion_diag.magic=0x4d4f5437;g_motion_diag.quantum=416;snapshot();return 0;
}
void gba_motion_level(dok_runtime *world)
{
    player=player_actor(world);motion.remembered_flags=0;
    dok_steps_init(&steps,gba_time_ticks(),416);pending_pressed=0;
    if(player) snapshot();
}
void gba_motion_poll(uint16_t keys)
{
    dok_input_sample(&input,keys);pending_pressed|=input.pressed;
    g_motion_diag.held=input.held;g_motion_diag.pressed=input.pressed;
    g_motion_diag.released=input.released;g_motion_diag.seen|=keys;
    if(g_motion_probe.test_mode) {
        dok_steps_init(&steps,gba_time_ticks(),416);
        if(g_motion_probe.request!=g_motion_probe.ack) {
            unsigned count=g_motion_probe.count;
            if(count>128) {g_motion_diag.error=2;return;}
            for(unsigned i=0;i<count;++i) {
                dok_motion_update(player,&motion,g_motion_probe.commands,416,0,0);
                g_motion_diag.steps++;
            }
            g_motion_diag.commands=g_motion_probe.commands;snapshot();
            g_motion_probe.ack=g_motion_probe.request;
        }
    } else {
        dok_steps_sample(&steps,gba_time_ticks());
        for(unsigned i=0;i<128 && dok_steps_next(&steps);++i) {
            dok_input sample=input;sample.pressed=pending_pressed;
            uint32_t commands=dok_input_commands(&sample);pending_pressed=0;
            dok_motion_update(player,&motion,commands,416,0,0);
            g_motion_diag.commands=commands;++g_motion_diag.steps;
        }
        snapshot();
    }
    g_motion_diag.debt=steps.debt;
}
void gba_motion_cost(uint32_t cycles)
{
    g_motion_diag.last_cycles=cycles;g_motion_diag.total_cycles+=cycles;
    if(cycles>g_motion_diag.max_cycles) g_motion_diag.max_cycles=cycles;
}
