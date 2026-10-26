#include "dok/collision.h"
#include "dok/clock.h"
#include "dok/interactions.h"
#include "dok/items.h"
extern uint32_t gba_time_ticks(void);
extern void gba_visual_poll(uint32_t tick);
extern unsigned dok_movement_cases(void);
extern unsigned dok_collision_cases(dok_runtime *);
extern unsigned dok_interactions_cases(dok_runtime *,dok_interactions *);
static dok_interactions interactions;
static dok_items items;
extern void gba_combat_poll(uint32_t tick,uint32_t commands);
extern void gba_combat_cycle(void);
dok_items *gba_items_context(void) {return &items;}
volatile struct {uint32_t magic,error,events,kind,object,category,value,pickups;} g_items_diag;
static void item_notice(void *p,dok_item_kind kind,uint16_t id,int16_t category,int16_t value)
{
 (void)p;++g_items_diag.events;g_items_diag.kind=kind;g_items_diag.object=id;
 g_items_diag.category=(uint32_t)(int32_t)category;g_items_diag.value=(uint32_t)(int32_t)value;
 if(kind==DOK_ITEM_PICKUP) ++g_items_diag.pickups;
}
int gba_items_stat(int16_t category,int16_t value) {return dok_items_stat(&items,category,value);}
int gba_items_pickup(void) {return dok_items_pickup(&items);}
volatile struct {uint32_t magic,error,events,kind,object,a,b,visual_steps,now,pending;} g_interaction_diag;
static uint32_t visual_next,visual_phase,simulation_now;
static void notice(void *p,dok_interaction_kind kind,uint16_t id,int32_t a,int32_t b)
{
    (void)p;g_interaction_diag.events++;g_interaction_diag.kind=kind;
    g_interaction_diag.object=id;g_interaction_diag.a=(uint32_t)a;g_interaction_diag.b=(uint32_t)b;
}
static void interaction_start(dok_runtime *w)
{
    simulation_now=gba_time_ticks();visual_next=simulation_now+333;visual_phase=0;
    dok_interactions_init(&interactions,w,simulation_now,notice,0);
    dok_items_init(&items,w,item_notice,0);g_items_diag.magic=0x4954454d;
    g_interaction_diag.magic=0x494e5439;
}
static void interaction_advance(uint32_t tick)
{
    gba_visual_poll(tick);
    for(unsigned i=0;i<128 && (int32_t)(tick-visual_next)>=0;i++) {
        dok_interactions_poll(&interactions,visual_next);
        visual_phase=(visual_phase+1)%3;visual_next+=visual_phase==2?334:333;
    }
    g_interaction_diag.error=interactions.errors;g_interaction_diag.now=interactions.now;
    g_interaction_diag.visual_steps=interactions.visual_steps;
    g_interaction_diag.pending=interactions.scheduler.owner_count;
}
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
static dok_runtime *collision_world;

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
    const dok_runtime_level *level=world->level;
    g_motion_diag.error=dok_collision_cases(world);
    if(!g_motion_diag.error) g_motion_diag.error=dok_interactions_cases(world,&interactions);
    if(dok_runtime_reset(world,level)!=DOK_RUNTIME_OK) return 3;
    if(g_motion_diag.error) return g_motion_diag.error;
    collision_world=world;player=player_actor(world);if(!player) return 1;
    motion.remembered_flags=0;input=(dok_input){0};pending_pressed=0;
    dok_steps_init(&steps,gba_time_ticks(),416);
    interaction_start(world);
    g_motion_diag.magic=0x4d4f5437;g_motion_diag.quantum=416;snapshot();return 0;
}
void gba_motion_level(dok_runtime *world)
{
    collision_world=world;player=player_actor(world);motion.remembered_flags=0;
    dok_steps_init(&steps,gba_time_ticks(),416);pending_pressed=0;
    interaction_start(world);
    if(player) snapshot();
}
void gba_motion_poll(uint16_t keys)
{
    dok_input_sample(&input,keys);pending_pressed|=input.pressed;
    if((keys&0x300)==0x300 && (input.pressed&0x300))gba_combat_cycle();
    g_motion_diag.held=input.held;g_motion_diag.pressed=input.pressed;
    g_motion_diag.released=input.released;g_motion_diag.seen|=keys;
    if(g_motion_probe.test_mode) {
        dok_steps_init(&steps,gba_time_ticks(),416);
        if(g_motion_probe.request!=g_motion_probe.ack) {
            unsigned count=g_motion_probe.count;
            if(count>128) {g_motion_diag.error=2;return;}
            for(unsigned i=0;i<count;++i) {
                simulation_now+=416;interaction_advance(simulation_now);
                if(g_motion_probe.commands&DOK_INPUT_USE) dok_interactions_use(&interactions,player);
                dok_items_pickup(&items);gba_combat_poll(simulation_now,g_motion_probe.commands);dok_interactions_zone(&interactions,player);
                dok_motion_update(player,&motion,g_motion_probe.commands,416,dok_collision_move,collision_world);
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
            simulation_now+=416;interaction_advance(simulation_now);
            if(commands&DOK_INPUT_USE) dok_interactions_use(&interactions,player);
            dok_items_pickup(&items);gba_combat_poll(simulation_now,commands);dok_interactions_zone(&interactions,player);
            dok_motion_update(player,&motion,commands,416,dok_collision_move,collision_world);
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
