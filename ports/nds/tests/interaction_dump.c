/* N11 isolated native callback transport. No expected physics here.
 * The scheduler fixture intercepts one callback exactly like the x86 oracle:
 * append/interval/cancel are recorded but appended callbacks are not executed.
 * This is deliberately not an independent test of accepted N08 dispatch. */
#include "dok/interactions.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static dok_level_definition definition;
static dok_initial_world world;
static dok_collision_state collision;
static dok_interactions_state interactions;
static dok_event_bank bank;
static dok_event_scheduler scheduler;
static const dok_object_ref target={1,2,1},source={1,3,1};
static uint32_t clock_tick;
static dok_status read_clock(void *context,uint32_t *out)
{(void)context;*out=clock_tick;return DOK_STATUS_OK;}
static void callback(void *context,dok_event_scheduler *queue,dok_object_ref owner,uint16_t id,uint32_t param)
{(void)context;(void)queue;(void)owner;(void)id;(void)param;}
static void add_field(dok_level_object *o,uint16_t offset,int32_t value)
{dok_level_field *f=&o->fields[o->field_count++];f->native_offset=offset;f->value=value;}
static int initialize(const int64_t *v)
{
    uint32_t i;
    dok_event_config config;
    dok_interaction_services services;
    memset(&definition,0,sizeof definition);memset(&world,0,sizeof world);
    memset(&collision,0,sizeof collision);memset(&interactions,0,sizeof interactions);
    memset(&config,0,sizeof config);memset(&services,0,sizeof services);
    definition.language=1;definition.object_count=3;
    definition.settings[0]=definition.settings[2]=4;definition.settings[1]=1;
    definition.settings[3]=480;definition.settings[7]=238;
    world.definition=&definition;world.epoch=1;world.object_count=3;world.hero=source;
    memset(world.maps,' ',sizeof world.maps);
    for(i=0;i<3;++i){
        unsigned char glyph=i==0?' ':i==1?'h':'@';
        definition.objects[i].glyph=glyph;definition.objects[i].flags=i==1?(uint16_t)v[16]:16;
        definition.objects[i].class_id=i==0?0:i==1?(v[0]==4?6:7):(uint16_t)v[13];
        definition.glyph_index[glyph]=(uint16_t)(i+1);world.glyph_index[glyph]=(uint16_t)(i+1);
        world.objects[i].reference=(dok_object_ref){1,i+1,1};world.objects[i].definition_index=(uint16_t)i;
        world.objects[i].class_id=definition.objects[i].class_id;
        world.objects[i].pose_known=1;world.objects[i].placements=1;
        world.objects[i].x=world.objects[i].z=720;
    }
    definition.objects[1].marker=(uint8_t)v[15];
    add_field(&definition.objects[1],0x52,(int32_t)v[10]);
    add_field(&definition.objects[1],0x5c,(int32_t)v[7]);
    add_field(&definition.objects[1],0x5e,(int32_t)v[8]);
    add_field(&definition.objects[1],0x66,(int32_t)(uint32_t)v[9]);
    add_field(&definition.objects[2],0x66,'h');
    add_field(&definition.objects[2],0x67,(int32_t)(uint32_t)v[11]);
    config.read_ticks=read_clock;config.callback=callback;config.world_epoch=1;
    clock_tick=0;
    if(dok_collision_bind(&collision,&world)!=DOK_STATUS_OK||
       dok_events_init(&scheduler,&bank,&config)!=DOK_STATUS_OK||
       dok_interactions_bind(&interactions,&world,&collision,&scheduler,&services)!=DOK_STATUS_OK||
       dok_interactions_register(&interactions)!=DOK_STATUS_OK)return 0;
    if(v[0]==4&&dok_events_register(&scheduler,target,4)!=DOK_STATUS_OK)return 0;
    if(dok_collision_set_flags(&collision,target,(uint16_t)v[1])!=DOK_STATUS_OK)return 0;
    /* Deliberately selected native object scalar state, outside dispatch. */
    collision.door_count['h']=(uint16_t)v[5];
    interactions.doors['h'].phase=(int16_t)v[2];interactions.doors['h'].state=(uint8_t)v[3];
    interactions.doors['h'].ready=(uint8_t)v[4];interactions.doors['h'].extent=(int16_t)v[6];
    interactions.doors['h'].marker=(uint8_t)v[15];
    return 1;
}
int main(void)
{
    int64_t v[17];
    for(;;){
        uint32_t i,bits,c2;int op;dok_status status;
        const dok_event_owner *owner;dok_event_owner *mutable_owner;
        int64_t out[18]={0};
        for(i=0;i<17;++i){long long value;int got=scanf("%lld",&value);
            if(got==EOF&&i==0)return fflush(stdout)==0?0:2;
            if(got!=1)return 2;
            v[i]=(int64_t)value;}
        op=(int)v[0];
        if(op==6){
            if(dok_interactions_cadence((uint32_t)v[14],(uint32_t)v[9],&c2)!=DOK_STATUS_OK)return 3;
            out[0]=c2;
        }else{
            if(!initialize(v))return 3;
            bits=(uint32_t)v[11];
            if(op>=1&&op<=3){
                uint16_t cb=(uint16_t)(DOK_INTERACTION_OPEN+op-1);
                if(dok_events_schedule(&scheduler,target,cb,0,0)!=DOK_STATUS_OK)return 3;
                if(dok_events_inspect_owner(&scheduler,target,&owner)!=DOK_STATUS_OK)return 3;
                /* Isolated callback invocation fixture, never product runtime. */
                scheduler._current=(uint16_t)(owner-bank.owners);scheduler._cursor=0;
                scheduler._in_callback=1;scheduler._dispatching=1;scheduler._keep=1;
                status=dok_interactions_callback(&interactions,target,cb);
                scheduler._in_callback=0;scheduler._dispatching=0;
                mutable_owner=&bank.owners[scheduler._current];
                if(scheduler._keep==0&&mutable_owner->count!=0){
                    --mutable_owner->count;
                    for(i=0;i<mutable_owner->count;++i)mutable_owner->events[i]=mutable_owner->events[i+1];
                }
            }else if(op==5){
                dok_motion_state motion;memset(&motion,0,sizeof motion);motion.x=motion.z=720;
                world.maps[DOK_WORLD_FCMAP][5]='@';
                status=dok_interactions_floor(&interactions,source,&motion,0,(uint32_t)v[14]);
            }else status=dok_interactions_event(&interactions,target,source,&bits,(uint16_t)v[12],(uint32_t)v[14]);
            if(status!=DOK_STATUS_OK)return 4;
            if(dok_events_inspect_owner(&scheduler,target,&owner)!=DOK_STATUS_OK)return 3;
            out[0]=collision.flags['h'];out[1]=interactions.doors['h'].phase;
            out[2]=interactions.doors['h'].state;out[3]=interactions.doors['h'].ready;
            out[4]=(int16_t)collision.door_count['h'];out[5]=interactions.doors['h'].extent;
            out[6]=(int16_t)v[10];out[7]=bits;out[8]=interactions.doors['h'].marker;out[9]=owner->count;
            for(i=0;i<owner->count;++i){out[10+i*2]=(int64_t)owner->events[i].callback-DOK_INTERACTION_OPEN+1;
                out[11+i*2]=owner->events[i].interval;}
        }
        for(i=0;i<18;++i)printf(i==17?"%lld\n":"%lld ",(long long)out[i]);
    }
}
