#include "interaction_router_cases.h"
#include <stddef.h>
#include <string.h>
#define HERO_CALLBACK 0x1201u
#define CHECK(t,c) do { \
    ++(t)->checks; \
    if(!(c)){ \
        ++(t)->failures; \
        if((t)->first_failed_line==0u){ \
            (t)->first_failed_line=(uint32_t)__LINE__; \
        } \
        return; \
    } \
} while(0)
static dok_status read_clock(void *context,uint32_t *out)
{ dok_interaction_router_workspace *w=context;*out=w->now;return DOK_STATUS_OK; }
static int trig(void *context,uint32_t heading,int32_t *sine,int32_t *cosine)
{ (void)context;if(heading!=0u)return 0;*sine=0;*cosine=32768;return 1; }
static void callback(void *context,dok_event_scheduler *scheduler,
    dok_object_ref owner,uint16_t id,uint32_t parameter)
{
    dok_interaction_router_workspace *w=context;dok_status status;
    (void)scheduler;(void)parameter;w->in_callback=1;
    if(id==HERO_CALLBACK){
        dok_control_commands commands;
        status=dok_interactions_floor(&w->interactions,w->world.hero,&w->motion,0,8);
        if(status==DOK_STATUS_OK)status=dok_controls_take(&w->hero,&commands);
        if(status==DOK_STATUS_OK){
            uint32_t at=w->hero_calls++;
            if(at<8u){
                w->hero_forward[at]=(commands.native_commands&DOK_INPUT_NATIVE_FORWARD)!=0u;
                w->hero_fire[at]=(commands.native_commands&DOK_INPUT_NATIVE_FIRE)!=0u;
                /* Main-action latch owns Use/weapon edges. Hero consumes only
                 * its movement/fire/run view; keep unrelated fronts pending. */
                w->hero_action_bits[at]=commands.native_commands&~(uint32_t)DOK_INPUT_NATIVE_USE;
            }
        }
    }else{++w->door_calls;status=dok_interactions_callback(&w->interactions,owner,id);}
    if(status!=DOK_STATUS_OK)++w->callback_errors;
    w->in_callback=0;
}
static void fixture(dok_interaction_router_workspace *w)
{
    static const unsigned char glyphs[3]={32,'H','D'};
    static const uint16_t classes[3]={0,1,7},flags[3]={16,0,64};
    dok_event_config config;dok_interaction_services services;uint32_t i;
    memset(w,0,sizeof(*w));w->definition.language=1;w->definition.object_count=3;
    w->definition.settings[0]=5;w->definition.settings[1]=1;w->definition.settings[2]=5;
    w->definition.settings[3]=480;w->definition.settings[4]=480;w->definition.settings[7]=238;
    w->world.definition=&w->definition;w->world.epoch=23;w->world.object_count=3;
    memset(w->world.maps,32,sizeof(w->world.maps));
    for(i=0;i<3u;++i){dok_world_object_seed *seed=&w->world.objects[i];
        w->definition.objects[i].glyph=glyphs[i];w->definition.objects[i].class_id=classes[i];
        w->definition.objects[i].flags=flags[i];
        w->definition.glyph_index[glyphs[i]]=(uint16_t)(i+1u);w->world.glyph_index[glyphs[i]]=(uint16_t)(i+1u);
        seed->definition_index=(uint16_t)i;seed->class_id=classes[i];
        seed->reference.world_epoch=23;seed->reference.object_id=100u+i;seed->reference.generation=1;
        seed->pose_known=(uint16_t)(classes[i]==1u);seed->x=1200;seed->y=-300;seed->z=1200;
    }
    w->world.hero=w->world.objects[1].reference;w->world.maps[DOK_WORLD_MMAP][12]='H';
    w->world.maps[DOK_WORLD_TMAP][17]='D';
    (void)dok_input_buffer_initialize(&w->input);(void)dok_controls_initialize(&w->actions);
    (void)dok_controls_initialize(&w->hero);(void)dok_motion_init(&w->motion,1200,-300,1200,0,5,120);
    (void)dok_collision_bind(&w->collision,&w->world);
    memset(&config,0,sizeof(config));config.clock_context=w;config.read_ticks=read_clock;
    config.callback_context=w;config.callback=callback;config.world_epoch=23;
    (void)dok_events_init(&w->scheduler,&w->bank,&config);
    memset(&services,0,sizeof(services));services.trig=trig;
    (void)dok_interactions_bind(&w->interactions,&w->world,&w->collision,&w->scheduler,&services);
    (void)dok_interactions_register(&w->interactions);
    (void)dok_events_register(&w->scheduler,w->world.hero,4);
    (void)dok_events_schedule(&w->scheduler,w->world.hero,HERO_CALLBACK,0,416);
}
static dok_status capture(dok_interaction_router_workspace *w,uint32_t held)
{
    dok_input_frame frame;dok_status status;
    memset(&frame,0,sizeof(frame));frame.sequence=++w->sequence;
    frame.held=held;frame.pressed=held&~w->raw_held;frame.released=w->raw_held&~held;
    status=dok_input_capture(&w->input,&frame,++w->counter);
    if(status==DOK_STATUS_OK)w->raw_held=held;
    return status;
}
static dok_status drain(dok_interaction_router_workspace *w)
{
    dok_input_sample sample;uint32_t available;dok_status status;
    do{
        status=dok_input_pop_through(&w->input,w->counter,&sample,&available);
        if(status!=DOK_STATUS_OK||available==0u)return status;
        status=dok_controls_feed(&w->actions,&sample.frame);
        if(status!=DOK_STATUS_OK)return status;
        status=dok_controls_feed(&w->hero,&sample.frame);
        if(status!=DOK_STATUS_OK)return status;
    }while(available!=0u);
    return DOK_STATUS_OK;
}
static dok_status service(dok_interaction_router_workspace *w,uint32_t now)
{
    dok_event_dispatch_report dispatch;dok_control_commands commands;uint32_t bits;
    dok_status status=drain(w);if(status!=DOK_STATUS_OK)return status;
    w->now=now;status=dok_events_dispatch(&w->scheduler,&dispatch);
    if(status!=DOK_STATUS_OK)return status;
    status=dok_controls_take(&w->actions,&commands);if(status!=DOK_STATUS_OK)return status;
    if(commands.use_triggered!=0u){
        ++w->main_uses;if(w->in_callback!=0u)++w->use_inside_callback;
        bits=DOK_INPUT_NATIVE_USE;
        status=dok_interactions_use(&w->interactions,w->world.hero,&w->motion,&bits,0,8);
        if(status!=DOK_STATUS_OK)return status;
        if(bits!=0u)return DOK_STATUS_INVALID_STATE;
    }
    return w->callback_errors==0u?DOK_STATUS_OK:DOK_STATUS_INVALID_STATE;
}
static dok_status boundary(dok_interaction_router_workspace *w)
{
    dok_status status=dok_input_ui_boundary(&w->input);
    if(status!=DOK_STATUS_OK)return status;
    status=dok_controls_ui_boundary(&w->actions);
    if(status!=DOK_STATUS_OK)return status;
    return dok_controls_ui_boundary(&w->hero);
}
static uint32_t door_events(dok_interaction_router_workspace *w)
{
    const dok_event_owner *owner=NULL;
    if(dok_events_inspect_owner(&w->scheduler,w->world.objects[2].reference,&owner)!=DOK_STATUS_OK)return UINT32_MAX;
    return owner->count;
}
static void pulses(dok_interaction_router_workspace *w,dok_interaction_router_report *t)
{
    fixture(w);++t->pulse_cases;
    CHECK(t,capture(w,DOK_BUTTON_UP|DOK_BUTTON_R)==DOK_STATUS_OK);
    CHECK(t,capture(w,0)==DOK_STATUS_OK);
    CHECK(t,capture(w,DOK_BUTTON_UP|DOK_BUTTON_R)==DOK_STATUS_OK);
    CHECK(t,capture(w,0)==DOK_STATUS_OK);
    CHECK(t,service(w,50)==DOK_STATUS_OK&&w->hero_calls==0u);
    CHECK(t,service(w,60)==DOK_STATUS_OK&&w->hero_calls==0u);
    CHECK(t,service(w,416)==DOK_STATUS_OK&&w->hero_calls==1u);
    CHECK(t,service(w,832)==DOK_STATUS_OK&&w->hero_calls==2u);
    CHECK(t,service(w,1248)==DOK_STATUS_OK&&w->hero_calls==3u);
    CHECK(t,w->hero_forward[0]==1u&&w->hero_forward[1]==1u&&w->hero_forward[2]==0u);
    CHECK(t,w->hero_fire[0]==1u&&w->hero_fire[1]==1u&&w->hero_fire[2]==0u);
    CHECK(t,w->main_uses==0u&&door_events(w)==0u);
}
static void uses(dok_interaction_router_workspace *w,dok_interaction_router_report *t)
{
    dok_interaction_phase_view phases;
    fixture(w);++t->use_cases;
    CHECK(t,capture(w,DOK_BUTTON_X|DOK_BUTTON_B)==DOK_STATUS_OK);
    CHECK(t,service(w,10)==DOK_STATUS_OK&&w->main_uses==0u);
    CHECK(t,service(w,20)==DOK_STATUS_OK&&w->main_uses==0u); /* held does not repeat */
    CHECK(t,capture(w,0)==DOK_STATUS_OK);
    CHECK(t,service(w,30)==DOK_STATUS_OK&&w->main_uses==1u&&door_events(w)==1u);
    CHECK(t,w->door_calls==0u&&w->use_inside_callback==0u);
    CHECK(t,dok_interactions_phases(&w->interactions,&phases)==DOK_STATUS_OK&&phases.phase['D']==0);
    CHECK(t,service(w,445)==DOK_STATUS_OK&&w->door_calls==0u);
    CHECK(t,service(w,446)==DOK_STATUS_OK&&w->door_calls==1u);
    CHECK(t,w->main_uses==1u&&w->hero_calls==1u&&(w->hero_action_bits[0]&DOK_INPUT_NATIVE_USE)==0u);
    fixture(w);++t->use_cases;
    CHECK(t,capture(w,DOK_BUTTON_X|DOK_BUTTON_B)==DOK_STATUS_OK);
    CHECK(t,capture(w,DOK_BUTTON_B)==DOK_STATUS_OK); /* X release */
    CHECK(t,capture(w,0)==DOK_STATUS_OK); /* separate B release */
    CHECK(t,service(w,50)==DOK_STATUS_OK&&w->main_uses==1u&&door_events(w)==1u);
    CHECK(t,service(w,51)==DOK_STATUS_OK&&w->main_uses==2u&&door_events(w)==2u);
    CHECK(t,service(w,52)==DOK_STATUS_OK&&w->main_uses==2u&&door_events(w)==2u);
    CHECK(t,w->door_calls==0u&&w->use_inside_callback==0u);
}
static void pauses(dok_interaction_router_workspace *w,dok_interaction_router_report *t)
{
    fixture(w);++t->pause_cases;
    CHECK(t,capture(w,DOK_BUTTON_UP|DOK_BUTTON_X)==DOK_STATUS_OK);
    CHECK(t,capture(w,0)==DOK_STATUS_OK);
    CHECK(t,drain(w)==DOK_STATUS_OK); /* pending pulse + release Use before pause */
    CHECK(t,boundary(w)==DOK_STATUS_OK); /* pause enter */
    CHECK(t,boundary(w)==DOK_STATUS_OK); /* return */
    CHECK(t,capture(w,0)==DOK_STATUS_OK);
    CHECK(t,service(w,416)==DOK_STATUS_OK&&w->main_uses==0u&&w->hero_forward[0]==0u);
    CHECK(t,door_events(w)==0u);
    fixture(w);++t->pause_cases;
    CHECK(t,capture(w,DOK_BUTTON_UP|DOK_BUTTON_X)==DOK_STATUS_OK);
    CHECK(t,boundary(w)==DOK_STATUS_OK); /* held through pause */
    CHECK(t,capture(w,DOK_BUTTON_UP|DOK_BUTTON_X)==DOK_STATUS_OK);
    CHECK(t,boundary(w)==DOK_STATUS_OK); /* each UI poll */
    CHECK(t,boundary(w)==DOK_STATUS_OK); /* return while still held */
    CHECK(t,capture(w,0)==DOK_STATUS_OK); /* swallowed release */
    CHECK(t,service(w,416)==DOK_STATUS_OK&&w->main_uses==0u&&w->hero_forward[0]==0u);
    CHECK(t,capture(w,DOK_BUTTON_UP|DOK_BUTTON_X)==DOK_STATUS_OK);
    CHECK(t,capture(w,0)==DOK_STATUS_OK);
    CHECK(t,service(w,500)==DOK_STATUS_OK&&w->main_uses==1u&&w->hero_calls==1u);
    CHECK(t,service(w,832)==DOK_STATUS_OK&&w->hero_forward[1]==1u&&w->main_uses==1u);
    CHECK(t,service(w,1248)==DOK_STATUS_OK&&w->hero_forward[2]==0u&&w->main_uses==1u);
    CHECK(t,w->callback_errors==0u&&w->use_inside_callback==0u);
}
int dok_interaction_router_run_cases(dok_interaction_router_workspace *w,dok_interaction_router_report *t)
{
    if(w==NULL||t==NULL)return 2;
    memset(t,0,sizeof(*t));pulses(w,t);uses(w,t);pauses(w,t);
    return t->failures==0u?0:1;
}
