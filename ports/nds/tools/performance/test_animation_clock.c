#include "../../tests/interaction_cases.c"
#include "gameplay.h"
#include "effects_runtime.h"
#include "animation_clock.h"
#include "fps_counter.h"
#include <stdio.h>
#include <stdlib.h>
#define VERIFY(e) do{++checks;if(!(e)){fprintf(stderr,"line%d: %s\n",__LINE__,#e);exit(1);}}while(0)
static unsigned checks;
static dok_interaction_test_workspace workspace;
static NdsGameplay game;
static NdsEffectsRuntime effects;
static void timed_callback(void *p,dok_event_scheduler *s,dok_object_ref ref,uint16_t kind,uint32_t parameter)
{(void)p;(void)s;VERIFY(nds_gameplay_callback(&game,&effects,ref,kind,parameter)==DOK_STATUS_OK);}
dok_status nds_effects_arm_bounds(NdsEffectsRuntime *r,const dok_render_gather_bounds *b,const dok_sprite_list *s)
{(void)r;(void)b;(void)s;abort();}
int main(void)
{
    dok_interaction_test_report baseline;
    VERIFY(dok_interaction_run_cases(&workspace,&baseline)==0);
    for(uint32_t rate=8;rate<=96;rate+=22)for(uint32_t cadence=0;cadence<4;++cadence){
        fixture(&workspace);memset(&game,0,sizeof(game));memset(&effects,0,sizeof(effects));
        game.items.world=&workspace.world;game.interactions=workspace.interactions;
        effects.scheduler=workspace.scheduler;game.interactions._scheduler=&effects.scheduler;
        effects.scheduler._config.callback=timed_callback;
        set_field(&workspace.definition.objects[3],0x52,5);
        uint32_t bits=0x800;
        VERIFY(dok_interactions_event(&game.interactions,workspace.world.objects[3].reference,
            workspace.world.hero,&bits,0,rate)==DOK_STATUS_OK);
        dok_interaction_door *door=&game.interactions.doors['D'];
        uint32_t frame=0,first=0,step=0;
        while(door->state!=1u){
            step=cadence==0?167u:cadence==1?334u:cadence==2?(frame%5u?167u:800u):(frame==3?5000u:167u);
            workspace.now+=step;++frame;
            dok_event_dispatch_report report;
            VERIFY(dok_events_dispatch(&effects.scheduler,&report)==DOK_STATUS_OK);
            if(!first&&door->phase)first=workspace.now;
            VERIFY(workspace.now<14000u);
        }
        VERIFY(door->ready&&door->phase==door->extent-1);
        uint32_t duration=(rate-1u)*1193182u/(119u*rate);
        VERIFY(workspace.now-first>=duration&&workspace.now-first<=duration+step+1u);
        VERIFY(workspace.collision.flags['D']&16u);
        /* Closing uses the same clock, including the blocked-door branch. */
        bits=0x800;VERIFY(dok_interactions_event(&game.interactions,workspace.world.objects[3].reference,
            workspace.world.hero,&bits,0,rate)==DOK_STATUS_OK);
        VERIFY(dok_collision_set_door_count(&workspace.collision,workspace.world.objects[3].reference,1)==DOK_STATUS_OK);
        workspace.now+=1000u;dok_event_dispatch_report report;
        VERIFY(dok_events_dispatch(&effects.scheduler,&report)==DOK_STATUS_OK&&door->ready);
        VERIFY(dok_collision_set_door_count(&workspace.collision,workspace.world.objects[3].reference,0)==DOK_STATUS_OK);
        first=0;
        while(door->state!=2u){
            workspace.now+=334u;
            VERIFY(dok_events_dispatch(&effects.scheduler,&report)==DOK_STATUS_OK);
            if(!first&&!door->ready)first=workspace.now;
            VERIFY(workspace.now<30000u);
        }
        VERIFY(workspace.now-first>=duration&&workspace.now-first<=duration+335u&&door->ready&&door->phase==0);
    }
    for(uint32_t cadence=167;cadence<=668;cadence+=167){
        uint32_t fraction=0,steps=0,ticks=0;NdsFpsCounter fps={0};
        nds_fps_present(&fps,1,UINT32_MAX-500u);
        while(ticks<100270u){
            steps+=nds_animation_steps(cadence,60,&fraction);ticks+=cadence;
            nds_fps_present(&fps,1,UINT32_MAX-500u+ticks);
        }
        VERIFY(steps==(uint64_t)ticks*7140u/1193182u);
        VERIFY(fps.tenths+1u>=100270u/cadence&&fps.tenths<=100270u/cadence+1u);
        uint32_t held=fps.tenths;nds_fps_present(&fps,1,fps.last);VERIFY(fps.tenths==held);
        nds_fps_present(&fps,2,0);VERIFY(!fps.ticks&&!fps.frames&&!fps.tenths);
    }
    printf("{\"passed\":true,\"checks\":%u,\"core_regression_checks\":%u}\n",checks,baseline.checks);
    return 0;
}
