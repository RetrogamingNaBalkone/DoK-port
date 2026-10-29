#include "interaction_cases.h"
#include <limits.h>
#include <stddef.h>
#include <string.h>
#define CHECK(r,c) do { \
    ++(r)->checks; \
    if(!(c)){ \
        ++(r)->failures; \
        if((r)->first_failed_line==0u){ \
            (r)->first_failed_line=(uint32_t)__LINE__; \
        } \
        return; \
    } \
} while(0)
static dok_status clock_read(void *context,uint32_t *out)
{ dok_interaction_test_workspace *w=context;*out=w->now;return DOK_STATUS_OK; }
static void sink(void *context,const dok_interaction_output *out)
{
    dok_interaction_test_workspace *w=context;
    if(w->output_count<32u)w->outputs[w->output_count]=*out;
    ++w->output_count;
    if(dok_interactions_phases(&w->interactions,&w->phases)==DOK_STATUS_INVALID_STATE)
        ++w->reentry_checks;
}
static void callback(void *context,dok_event_scheduler *scheduler,dok_object_ref owner,
    uint16_t id,uint32_t parameter)
{
    dok_interaction_test_workspace *w=context;
    (void)scheduler;(void)parameter;++w->callbacks;
    if(dok_interactions_callback(&w->interactions,owner,id)!=DOK_STATUS_OK)++w->callback_errors;
}
static int trig(void *context,uint32_t heading,int32_t *sine,int32_t *cosine)
{
    (void)context;
    if(heading==0u){*sine=0;*cosine=32768;return 1;}
    if(heading==90u){*sine=32768;*cosine=0;return 1;}
    return 0;
}
static void set_field(dok_level_object *o,uint16_t offset,int32_t value)
{
    uint32_t i;
    for(i=0;i<o->field_count;++i)if(o->fields[i].native_offset==offset){o->fields[i].value=value;return;}
    i=o->field_count++;o->fields[i].native_offset=offset;o->fields[i].value=value;
}
static void fixture(dok_interaction_test_workspace *w)
{
    static const uint8_t glyph[7]={32,'H','#','D','S','F','W'};
    static const uint16_t classes[7]={0,1,2,7,13,12,6};
    static const uint16_t flags[7]={16,0,2,64,16,16,0};
    uint32_t i;dok_event_config config;dok_interaction_services services;
    memset(w,0,sizeof(*w));w->definition.language=1;w->definition.object_count=7;
    w->definition.settings[0]=5;w->definition.settings[1]=1;w->definition.settings[2]=5;
    w->definition.settings[3]=480;w->definition.settings[4]=480;w->definition.settings[7]=238;
    w->world.definition=&w->definition;w->world.epoch=11;w->world.object_count=7;
    memset(w->world.maps,32,sizeof(w->world.maps));
    for(i=0;i<7u;++i){dok_world_object_seed *o=&w->world.objects[i];
        w->definition.objects[i].glyph=glyph[i];w->definition.objects[i].class_id=classes[i];
        w->definition.objects[i].flags=flags[i];w->definition.glyph_index[glyph[i]]=(uint16_t)(i+1u);
        w->world.glyph_index[glyph[i]]=(uint16_t)(i+1u);o->definition_index=(uint16_t)i;
        o->class_id=classes[i];o->reference.world_epoch=11;o->reference.object_id=i+1u;o->reference.generation=1;
        o->pose_known=(uint16_t)(classes[i]==1u);o->x=1200;o->y=-300;o->z=1200;
    }
    w->world.hero=w->world.objects[1].reference;w->world.maps[DOK_WORLD_MMAP][12]='H';
    w->definition.objects[3].marker=0x55;
    set_field(&w->definition.objects[4],0x66,'D');set_field(&w->definition.objects[4],0x67,0x800);
    memset(&config,0,sizeof(config));config.clock_context=w;config.read_ticks=clock_read;
    config.callback_context=w;config.callback=callback;config.world_epoch=11;
    (void)dok_collision_bind(&w->collision,&w->world);
    (void)dok_events_init(&w->scheduler,&w->bank,&config);
    memset(&services,0,sizeof(services));services.sink_context=w;services.sink=sink;
    services.move_context=&w->collision;services.move=dok_interactions_move;services.trig=trig;
    (void)dok_interactions_bind(&w->interactions,&w->world,&w->collision,&w->scheduler,&services);
    (void)dok_interactions_register(&w->interactions);
}
static dok_motion_state motion(void)
{ dok_motion_state m;(void)dok_motion_init(&m,1200,-300,1200,0,5,120);return m; }
static dok_status dispatch(dok_interaction_test_workspace *w,uint32_t now)
{ dok_event_dispatch_report report;w->now=now;return dok_events_dispatch(&w->scheduler,&report); }
static dok_status use(dok_interaction_test_workspace *w,uint16_t keys,uint32_t c2)
{ uint32_t bits=0x800;return dok_interactions_event(&w->interactions,w->world.objects[3].reference,
    w->world.hero,&bits,keys,c2); }
static uint32_t events(dok_interaction_test_workspace *w)
{ const dok_event_owner *owner=NULL;(void)dok_events_inspect_owner(&w->scheduler,w->world.objects[3].reference,&owner);
  return owner!=NULL?owner->count:UINT32_MAX; }
static void doors(dok_interaction_test_workspace *w,dok_interaction_test_report *t)
{
    uint32_t bits,i;dok_interaction_door *d;
    fixture(w);d=&w->interactions.doors['D'];++t->door_cases;
    CHECK(t,d->phase==0&&d->extent==1&&d->state==2&&d->ready==1&&d->marker==0x55);
    CHECK(t,w->scheduler._registered_count==1u&&events(w)==0u);
    set_field(&w->definition.objects[3],0x5c,3);
    bits=0x40800;CHECK(t,dok_interactions_event(&w->interactions,w->world.objects[3].reference,
        w->world.hero,&bits,0,30)==DOK_STATUS_OK);
    CHECK(t,bits==0&&d->extent==30&&events(w)==0u&&w->output_count==2u);
    CHECK(t,w->outputs[1].kind==DOK_INTERACTION_LOCKED&&w->outputs[1].key_mask==3u);
    CHECK(t,w->reentry_checks==2u);
    CHECK(t,use(w,2,8)==DOK_STATUS_OK&&events(w)==1u); /* any key bit, not all */
    CHECK(t,d->ready==1&&d->phase==0);
    CHECK(t,dispatch(w,415)==DOK_STATUS_OK&&d->phase==0);
    CHECK(t,dispatch(w,416)==DOK_STATUS_OK&&d->phase==1&&d->ready==0);
    CHECK(t,(w->collision.flags['D']&0x18u)==8u);
    bits=0x800;CHECK(t,dok_interactions_event(&w->interactions,w->world.objects[3].reference,
        w->world.hero,&bits,2,60)==DOK_STATUS_OK&&bits==0x800&&d->extent==8);
    CHECK(t,dispatch(w,417)==DOK_STATUS_OK&&d->phase==2&&(w->collision.flags['D']&16u)==0u);
    CHECK(t,dispatch(w,418)==DOK_STATUS_OK&&d->phase==3&&(w->collision.flags['D']&16u)!=0u);
    for(i=419;i<=423u;++i)CHECK(t,dispatch(w,i)==DOK_STATUS_OK);
    CHECK(t,d->phase==7&&d->state==1&&d->ready==1&&events(w)==0u&&d->marker==0xaau);
    CHECK(t,use(w,2,8)==DOK_STATUS_OK&&events(w)==1u);
    CHECK(t,dok_collision_set_door_count(&w->collision,w->world.objects[3].reference,1)==DOK_STATUS_OK);
    CHECK(t,dispatch(w,839)==DOK_STATUS_OK&&d->phase==7&&d->ready==1);
    CHECK(t,dok_collision_set_door_count(&w->collision,w->world.objects[3].reference,UINT16_MAX)==DOK_STATUS_OK);
    CHECK(t,dispatch(w,1255)==DOK_STATUS_OK&&d->phase==6&&d->ready==0); /* signed -1 */
    for(i=1256;i<=1262u;++i)CHECK(t,dispatch(w,i)==DOK_STATUS_OK);
    CHECK(t,d->phase==0&&d->state==2&&d->ready==1&&d->marker==0x55&&events(w)==0u);
    CHECK(t,w->collision.flags['D']==64u&&w->callback_errors==0u);
    CHECK(t,dok_interactions_phases(&w->interactions,&w->phases)==DOK_STATUS_OK&&
        w->phases.extent['D']==8&&w->phases.phase['D']==0&&w->phases.extent['W']==0);
    fixture(w);++t->door_cases;
    CHECK(t,use(w,0,30)==DOK_STATUS_OK&&use(w,0,30)==DOK_STATUS_OK&&use(w,0,30)==DOK_STATUS_OK);
    CHECK(t,events(w)==3u&&use(w,0,60)==DOK_STATUS_OK&&events(w)==3u&&
        w->interactions.schedule_refusals==1u&&w->interactions.doors['D'].extent==60);
    fixture(w);++t->door_cases;
    set_field(&w->definition.objects[3],0x5e,1);set_field(&w->definition.objects[3],0x66,1000);
    CHECK(t,use(w,0,8)==DOK_STATUS_OK&&events(w)==2u);
    for(i=416;i<=423u;++i)CHECK(t,dispatch(w,i)==DOK_STATUS_OK);
    CHECK(t,w->interactions.doors['D'].state==1u&&events(w)==1u);
    CHECK(t,dispatch(w,1000)==DOK_STATUS_OK&&w->interactions.doors['D'].phase==7&&events(w)==1u);
    CHECK(t,dispatch(w,1999)==DOK_STATUS_OK&&w->interactions.doors['D'].phase==7);
    CHECK(t,dispatch(w,2000)==DOK_STATUS_OK&&w->interactions.doors['D'].phase==6);
    CHECK(t,w->callback_errors==0u);
}
static void floors(dok_interaction_test_workspace *w,dok_interaction_test_report *t)
{
    dok_motion_state m=motion(),before;uint32_t count,bits;dok_object_ref old;
    fixture(w);++t->floor_cases;w->world.maps[DOK_WORLD_FCMAP][12]='S';
    set_field(&w->definition.objects[3],0x5c,8);
    CHECK(t,dok_interactions_floor(&w->interactions,w->world.hero,&m,0,30)==DOK_STATUS_OK&&events(w)==1u);
    CHECK(t,w->output_count==0u); /* sensor bypasses key */
    m.x=1680;w->world.maps[DOK_WORLD_FCMAP][13]='S';
    CHECK(t,dok_interactions_floor(&w->interactions,w->world.hero,&m,0,30)==DOK_STATUS_OK&&events(w)==1u);
    old=w->interactions.previous_floor;m.x=1200;w->world.maps[DOK_WORLD_FCMAP][12]=32;
    CHECK(t,dok_interactions_floor(&w->interactions,w->world.hero,&m,0,30)==DOK_STATUS_OK&&
        w->interactions.previous_floor.object_id!=old.object_id);
    w->world.maps[DOK_WORLD_FCMAP][12]='S';
    CHECK(t,dok_interactions_floor(&w->interactions,w->world.hero,&m,0,30)==DOK_STATUS_OK&&events(w)==2u);
    fixture(w);++t->floor_cases;m=motion();before=m;w->world.maps[DOK_WORLD_FCMAP][12]='F';
    set_field(&w->definition.objects[5],0x66,100);set_field(&w->definition.objects[5],0x68,1400);
    CHECK(t,dok_interactions_floor(&w->interactions,w->world.hero,&m,0,30)==DOK_STATUS_OK);
    CHECK(t,m.x==1246&&m.z==1200&&(m.flags&DOK_MOTION_SLOW)!=0u);
    m.x=before.x;m.flags=before.flags;CHECK(t,memcmp(&m,&before,sizeof(m))==0);
    m=before;set_field(&w->definition.objects[5],0x66,102);set_field(&w->definition.objects[5],0x68,-1400);
    CHECK(t,dok_interactions_floor(&w->interactions,w->world.hero,&m,0,30)==DOK_STATUS_OK&&m.z==1154);
    m=before;w->world.maps[DOK_WORLD_TMAP][13]='#';
    set_field(&w->definition.objects[5],0x66,100);set_field(&w->definition.objects[5],0x68,1400);
    CHECK(t,dok_interactions_floor(&w->interactions,w->world.hero,&m,0,30)==DOK_STATUS_OK&&m.x==1201);
    count=w->collision._revision;before=m;
    CHECK(t,dok_interactions_move(&w->collision,w->world.hero,&m,m.x+350,m.z)==DOK_STATUS_OK);
    CHECK(t,w->collision._revision==count+1u&&m.x==1201&&m.z==before.z);
    fixture(w);m=motion();before=m;w->world.maps[DOK_WORLD_FCMAP][12]='F';
    set_field(&w->definition.objects[5],0x66,100);set_field(&w->definition.objects[5],0x68,1400);
    CHECK(t,dok_interactions_floor(&w->interactions,w->world.hero,&m,0,4)==DOK_STATUS_OK&&m.x==1550);
    m.x=before.x;m.flags=before.flags;CHECK(t,memcmp(&m,&before,sizeof(m))==0);
    fixture(w);++t->floor_cases;m=motion();w->world.maps[DOK_WORLD_FCMAP][12]='F';
    set_field(&w->definition.objects[5],0x66,1000);set_field(&w->definition.objects[5],0x68,1);
    w->world.category99_total=7;
    CHECK(t,dok_interactions_floor(&w->interactions,w->world.hero,&m,6,30)==DOK_STATUS_OK&&w->output_count==0u);
    CHECK(t,dok_interactions_floor(&w->interactions,w->world.hero,&m,7,30)==DOK_STATUS_OK&&w->output_count==1u);
    CHECK(t,w->outputs[0].kind==DOK_INTERACTION_FINISH&&w->outputs[0].value==0);
    CHECK(t,dok_interactions_floor(&w->interactions,w->world.hero,&m,7,30)==DOK_STATUS_OK&&w->output_count==2u);
    fixture(w);++t->floor_cases;m=motion();w->world.maps[DOK_WORLD_TMAP][17]='D';bits=0x800;
    CHECK(t,dok_interactions_use(&w->interactions,w->world.hero,&m,&bits,0,8)==DOK_STATUS_OK&&bits==0&&events(w)==1u);
    fixture(w);m=motion();m.heading=90;w->world.maps[DOK_WORLD_TMAP][11]='W';bits=0x800;
    CHECK(t,dok_interactions_use(&w->interactions,w->world.hero,&m,&bits,0,8)==DOK_STATUS_OK&&bits==0&&events(w)==0u);
}
static void guards(dok_interaction_test_workspace *w,dok_interaction_test_report *t)
{
    uint32_t c2=99,bits;dok_object_ref bad;dok_motion_state m=motion();
    fixture(w);++t->guard_cases;
    CHECK(t,dok_interactions_cadence(4,333,&c2)==DOK_STATUS_OK&&c2==17u);
    CHECK(t,dok_interactions_cadence(UINT32_MAX,1,&c2)==DOK_STATUS_OK&&c2==4999u);
    CHECK(t,dok_interactions_cadence(0,10000,&c2)==DOK_STATUS_OK&&c2==4u);
    c2=99;CHECK(t,dok_interactions_cadence(4,0,&c2)==DOK_STATUS_INVALID_ARGUMENT&&c2==99u);
    bad=w->world.hero;++bad.generation;bits=0x800;
    CHECK(t,dok_interactions_use(&w->interactions,bad,&m,&bits,0,30)==DOK_STATUS_STALE_REFERENCE&&bits==0x800);
    CHECK(t,dok_interactions_callback(&w->interactions,w->world.objects[3].reference,DOK_INTERACTION_OPEN)==DOK_STATUS_INVALID_STATE);
    bits=0x40800;CHECK(t,dok_interactions_event(&w->interactions,w->world.objects[6].reference,
        w->world.hero,&bits,0,30)==DOK_STATUS_OK&&bits==0x800);
    bits=0x40000;CHECK(t,dok_interactions_event(&w->interactions,w->world.objects[0].reference,
        w->world.hero,&bits,0,30)==DOK_STATUS_OK&&bits==0x40000);
    CHECK(t,use(w,0,0x8000)==DOK_STATUS_OK&&w->interactions.doors['D'].extent==8);
    ++w->world.epoch;CHECK(t,dok_interactions_phases(&w->interactions,&w->phases)==DOK_STATUS_STALE_REFERENCE);
}
static void direct_cases(dok_interaction_test_workspace *w,dok_interaction_test_report *t)
{
    dok_motion_state m=motion(),before;dok_collision_report report,saved;
    dok_collision_context active;dok_object_ref bad;
    fixture(w);++t->guard_cases;before=m;
    memset(&report,0x5a,sizeof(report));saved=report;
    bad=w->world.hero;++bad.generation;
    CHECK(t,dok_collision_displace(&w->collision,bad,&m,1550,1200,&report)==DOK_STATUS_STALE_REFERENCE);
    CHECK(t,memcmp(&m,&before,sizeof(m))==0&&memcmp(&report,&saved,sizeof(report))==0);
    w->world.maps[DOK_WORLD_MMAP][12]='#';
    CHECK(t,dok_collision_displace(&w->collision,w->world.hero,&m,1550,1200,&report)==DOK_STATUS_INVALID_STATE);
    CHECK(t,w->world.maps[DOK_WORLD_MMAP][12]=='#'&&memcmp(&report,&saved,sizeof(report))==0);
    w->world.maps[DOK_WORLD_MMAP][12]='H';w->collision._revision=UINT32_MAX;
    CHECK(t,dok_collision_displace(&w->collision,w->world.hero,&m,1550,1200,&report)==DOK_STATUS_LIMIT_REACHED);
    CHECK(t,memcmp(&m,&before,sizeof(m))==0&&memcmp(&report,&saved,sizeof(report))==0);
    fixture(w);++t->guard_cases;
    CHECK(t,dok_collision_begin(&w->collision,w->world.hero,&m,0,&active)==DOK_STATUS_OK);
    CHECK(t,dok_collision_displace(&w->collision,w->world.hero,&m,1550,1200,&report)==DOK_STATUS_INVALID_STATE);
    CHECK(t,dok_collision_end(&active,&report)==DOK_STATUS_OK&&report.committed==0u);
    CHECK(t,dok_collision_displace(&w->collision,w->world.hero,&m,1550,1200,&report)==DOK_STATUS_OK);
    CHECK(t,m.x==1550&&report.requested==1u&&report.committed==1u&&report.adjusted==0u&&report.rejected==0u);
    CHECK(t,w->world.maps[DOK_WORLD_MMAP][12]==32&&w->world.maps[DOK_WORLD_MMAP][13]=='H');
    fixture(w);++t->guard_cases;m=motion();w->world.maps[DOK_WORLD_TMAP][12]='D';
    CHECK(t,dok_collision_set_flags(&w->collision,w->world.objects[3].reference,16)==DOK_STATUS_OK);
    CHECK(t,dok_collision_displace(&w->collision,w->world.hero,&m,1200,1200,&report)==DOK_STATUS_OK);
    CHECK(t,w->collision.door_count['D']==1u&&report.committed==1u); /* exact0->1 samecell */
    CHECK(t,dok_collision_set_door_count(&w->collision,w->world.objects[3].reference,UINT16_MAX)==DOK_STATUS_OK);
    CHECK(t,dok_collision_displace(&w->collision,w->world.hero,&m,1550,1200,&report)==DOK_STATUS_OK);
    CHECK(t,w->collision.door_count['D']==UINT16_MAX-1u&&m.x==1550);
    fixture(w);++t->guard_cases;m=motion();m.x=240;
    w->world.maps[DOK_WORLD_MMAP][12]=32;w->world.maps[DOK_WORLD_MMAP][10]='H';
    CHECK(t,dok_collision_displace(&w->collision,w->world.hero,&m,-400,1200,&report)==DOK_STATUS_OK);
    CHECK(t,m.x==240&&report.requested==1u&&report.rejected==1u&&
        report.reason_mask==DOK_COLLISION_WORLD_EDGE&&report.committed==1u);
}
int dok_interaction_run_cases(dok_interaction_test_workspace *w,dok_interaction_test_report *t)
{
    if(w==NULL||t==NULL)return 2;
    memset(t,0,sizeof(*t));doors(w,t);floors(w,t);guards(w,t);direct_cases(w,t);
    return t->failures==0u?0:1;
}
