#include "dok/interactions.h"
#include <limits.h>
#include <stddef.h>
#include <string.h>
#define TAG UINT32_C(0x494e5431)
#define USE UINT32_C(0x800)
static int32_t signed32(uint32_t n)
{ return n<=INT32_MAX?(int32_t)n:(int32_t)((int64_t)n-INT64_C(4294967296)); }
static int16_t word(uint32_t n)
{ uint32_t v=(uint16_t)n;return v<=32767u?(int16_t)v:(int16_t)((int32_t)v-65536); }
static int32_t add32(int32_t a,int32_t b)
{ return signed32((uint32_t)a+(uint32_t)b); }
static int32_t sar15(int32_t n)
{ return n>=0?n/32768:(int32_t)(-((-(int64_t)n+32767)/32768)); }
static int32_t mul32(int32_t a,int32_t b)
{ return signed32((uint32_t)((uint64_t)(uint32_t)a*(uint32_t)b)); }
static uint32_t same(dok_object_ref a,dok_object_ref b)
{ return a.world_epoch==b.world_epoch&&a.object_id==b.object_id&&a.generation==b.generation; }
static uint32_t zero(dok_object_ref a)
{ return a.world_epoch==0u&&a.object_id==0u&&a.generation==0u; }
static const dok_world_object_seed *find(const dok_interactions_state *s,dok_object_ref ref)
{
    uint32_t i;
    for(i=0;i<s->_world->object_count;++i)
        if(same(s->_world->objects[i].reference,ref))return &s->_world->objects[i];
    return NULL;
}
static const dok_level_object *definition(const dok_interactions_state *s,
    const dok_world_object_seed *seed)
{ return &s->_world->definition->objects[seed->definition_index]; }
static int32_t field(const dok_level_object *o,uint16_t offset,int32_t fallback)
{ const dok_level_field *f=dok_level_field_at(o,offset);return f!=NULL?f->value:fallback; }
static dok_status valid(const dok_interactions_state *s)
{
    dok_collision_view view;
    if(s==NULL||s->_tag!=TAG||s->_world==NULL||s->_collision==NULL||s->_scheduler==NULL)
        return DOK_STATUS_INVALID_STATE;
    if(s->_world->epoch!=s->_epoch||s->_collision->_world!=s->_world)
        return DOK_STATUS_STALE_REFERENCE;
    if(s->_busy!=0u||s->_collision->_active!=NULL)return DOK_STATUS_INVALID_STATE;
    return dok_collision_get_view(s->_collision,&view);
}
static dok_status enter(dok_interactions_state *s)
{ dok_status r=valid(s);if(r!=DOK_STATUS_OK)return r;
  if(s->revision==UINT32_MAX)return DOK_STATUS_LIMIT_REACHED;
  s->_busy=1;return DOK_STATUS_OK; }
static dok_status leave(dok_interactions_state *s,dok_status status)
{ s->_busy=0;if(status!=DOK_STATUS_OK)s->fault=(uint32_t)status;return status; }
static void emit(dok_interactions_state *s,uint32_t kind,dok_object_ref object,
    dok_object_ref source,int16_t category,int16_t value,uint16_t key)
{
    dok_interaction_output out;
    memset(&out,0,sizeof(out));out.kind=kind;out.object=object;out.source=source;
    out.category=category;out.value=value;out.key_mask=key;
    if(s->_services.sink!=NULL)s->_services.sink(s->_services.sink_context,&out);
}
static dok_status schedule(dok_interactions_state *s,dok_object_ref owner,
    uint16_t callback,uint32_t interval)
{
    dok_status r=dok_events_schedule(s->_scheduler,owner,callback,0,interval);
    if(r==DOK_STATUS_NO_SPACE){++s->schedule_refusals;return DOK_STATUS_OK;}
    return r;
}
static dok_status event(dok_interactions_state *s,dok_object_ref target,
    dok_object_ref source,uint32_t *bits,uint16_t keys,uint32_t c2)
{
    const dok_world_object_seed *seed=find(s,target),*src=NULL;
    const dok_level_object *o;
    dok_interaction_door *d;
    uint16_t flags,key;
    dok_status r;
    if(seed==NULL)return DOK_STATUS_STALE_REFERENCE;
    if(!zero(source)){src=find(s,source);if(src==NULL)return DOK_STATUS_STALE_REFERENCE;}
    o=definition(s,seed);flags=s->_collision->flags[o->glyph];
    if(o->class_id!=7u){
        if((*bits&UINT32_C(0x40000))!=0u&&src!=NULL&&(flags&0x18u)==0u)
            *bits&=~UINT32_C(0x40000);
        return DOK_STATUS_OK;
    }
    if((*bits&UINT32_C(0x40000))!=0u&&(flags&8u)==0u)*bits&=~UINT32_C(0x40000);
    d=&s->doors[o->glyph];
    if((*bits&USE)==0u||d->ready==0u)return DOK_STATUS_OK;
    if(src==NULL)return DOK_STATUS_INVALID_ARGUMENT;
    d->extent=word(c2);if(d->extent<8)d->extent=8;
    ++s->revision;*bits&=~USE;
    key=(uint16_t)field(o,0x5c,0);
    if(src->class_id==13u)keys=UINT16_MAX;
    if(key!=0u&&(key&keys)==0u){
        emit(s,DOK_INTERACTION_SOUND_STOP,target,source,0,0,0);
        emit(s,DOK_INTERACTION_LOCKED,target,source,0,0,key);
        return DOK_STATUS_OK;
    }
    if(d->state==2u){
        r=schedule(s,target,DOK_INTERACTION_OPEN,416);
        if(r!=DOK_STATUS_OK)return r;
        if(word((uint32_t)field(o,0x5e,0))==1)
            return schedule(s,target,DOK_INTERACTION_AUTO,(uint32_t)field(o,0x66,0));
    }else if(word((uint32_t)field(o,0x5e,0))==0)
        return schedule(s,target,DOK_INTERACTION_CLOSE,416);
    return DOK_STATUS_OK;
}
dok_status dok_interactions_cadence(uint32_t old,uint32_t ticks,uint32_t *out)
{
    uint32_t n;
    if(ticks==0u||out==NULL)return DOK_STATUS_INVALID_ARGUMENT;
    n=(UINT32_C(10000)/ticks+old)>>1;
    if(signed32(n)<4)n=4;
    *out=n;return DOK_STATUS_OK;
}
dok_status dok_interactions_bind(dok_interactions_state *s,dok_initial_world *w,
    dok_collision_state *collision,dok_event_scheduler *scheduler,
    const dok_interaction_services *services)
{
    dok_collision_view view;uint32_t i;
    if(s==NULL||w==NULL||collision==NULL||scheduler==NULL||services==NULL)
        return DOK_STATUS_INVALID_ARGUMENT;
    if(collision->_world!=w||collision->_active!=NULL||
       dok_collision_get_view(collision,&view)!=DOK_STATUS_OK||
       scheduler->_config.world_epoch!=w->epoch||scheduler->_dispatching!=0u)
        return DOK_STATUS_INVALID_STATE;
    memset(s,0,sizeof(*s));s->_world=w;s->_collision=collision;s->_scheduler=scheduler;
    s->_services=*services;s->_tag=TAG;s->_epoch=w->epoch;s->revision=1;
    for(i=0;i<w->object_count;++i){
        const dok_level_object *o=&w->definition->objects[i];
        if(o->class_id==7u){dok_interaction_door *d=&s->doors[o->glyph];
            d->phase=0;d->extent=1;d->state=2;d->ready=1;d->marker=o->marker;}
    }
    return DOK_STATUS_OK;
}
dok_status dok_interactions_register(dok_interactions_state *s)
{
    uint32_t i;dok_status r=enter(s);
    if(r!=DOK_STATUS_OK)return r;
    if(s->_registered!=0u)return leave(s,DOK_STATUS_INVALID_STATE);
    for(i=0;i<s->_world->object_count;++i)if(s->_world->objects[i].class_id==7u){
        r=dok_events_register(s->_scheduler,s->_world->objects[i].reference,4);
        if(r!=DOK_STATUS_OK)return leave(s,r);
    }
    s->_registered=1;return leave(s,DOK_STATUS_OK);
}
dok_status dok_interactions_event(dok_interactions_state *s,dok_object_ref target,
    dok_object_ref source,uint32_t *bits,uint16_t keys,uint32_t c2)
{
    dok_status r;
    if(bits==NULL)return DOK_STATUS_INVALID_ARGUMENT;
    r=enter(s);if(r!=DOK_STATUS_OK)return r;
    r=event(s,target,source,bits,keys,c2);return leave(s,r);
}
static uint32_t map_index(const dok_interactions_state *s,int32_t x,int32_t z)
{
    const int16_t *p=s->_world->definition->settings;
    int32_t cx=word((uint32_t)(x/p[3])),cz=word((uint32_t)(z/p[3]));
    if(cx<0)cx=0;
    if(cx>=p[0])cx=p[0]-1;
    if(cz<0)cz=0;
    if(cz>=p[2])cz=p[2]-1;
    return (uint32_t)cz*(uint16_t)p[0]+(uint32_t)cx;
}
dok_status dok_interactions_use(dok_interactions_state *s,dok_object_ref hero,
    const dok_motion_state *motion,uint32_t *bits,uint16_t keys,uint32_t c2)
{
    const dok_world_object_seed *seed;int32_t sine,cosine,x,z,distance;
    uint16_t index;dok_status r;
    if(motion==NULL||bits==NULL)return DOK_STATUS_INVALID_ARGUMENT;
    r=enter(s);if(r!=DOK_STATUS_OK)return r;
    seed=find(s,hero);
    if(seed==NULL||!same(hero,s->_world->hero))return leave(s,DOK_STATUS_STALE_REFERENCE);
    if((*bits&USE)==0u)return leave(s,DOK_STATUS_OK);
    if(s->_services.trig==NULL||motion->heading<0||motion->heading>360||
       !s->_services.trig(s->_services.trig_context,(uint32_t)motion->heading,&sine,&cosine))
        return leave(s,DOK_STATUS_INVALID_STATE);
    distance=-s->_world->definition->settings[DOK_LEVEL_CELL_SIDE];
    x=add32(motion->x,sar15(mul32(distance,sine)));
    z=add32(motion->z,signed32(0u-(uint32_t)sar15(mul32(distance,cosine))));
    index=s->_world->glyph_index[s->_world->maps[DOK_WORLD_TMAP][map_index(s,x,z)]];
    if(index==0u||index>s->_world->object_count)return leave(s,DOK_STATUS_INVALID_STATE);
    r=event(s,s->_world->objects[index-1u].reference,hero,bits,keys,c2);
    *bits&=~USE;return leave(s,r);
}
dok_status dok_interactions_floor(dok_interactions_state *s,dok_object_ref hero,
    dok_motion_state *motion,int16_t score,uint32_t c2)
{
    const dok_world_object_seed *seed;const dok_level_object *o;uint16_t index;
    int16_t value,category;int32_t divisor,x,z;uint32_t bits;dok_status r;
    if(motion==NULL)return DOK_STATUS_INVALID_ARGUMENT;
    r=enter(s);if(r!=DOK_STATUS_OK)return r;
    seed=find(s,hero);
    if(seed==NULL||!same(hero,s->_world->hero))return leave(s,DOK_STATUS_STALE_REFERENCE);
    index=s->_world->glyph_index[s->_world->maps[DOK_WORLD_FCMAP][map_index(s,motion->x,motion->z)]];
    if(index==0u||index>s->_world->object_count)return leave(s,DOK_STATUS_INVALID_STATE);
    seed=&s->_world->objects[index-1u];o=definition(s,seed);
    if(o->class_id==12u){
        divisor=signed32(c2);if(divisor==0)return leave(s,DOK_STATUS_INVALID_ARGUMENT);
        value=word((uint32_t)(word((uint32_t)field(o,0x68,0))/divisor));
        category=word((uint32_t)field(o,0x66,0));motion->flags|=DOK_MOTION_SLOW;
        if(category==100||category==101||category==102){
            if(s->_services.move==NULL)return leave(s,DOK_STATUS_INVALID_STATE);
            x=motion->x;z=motion->z;
            if(category==102)z=add32(z,value);else x=add32(x,value);
            r=s->_services.move(s->_services.move_context,hero,motion,x,z);
            if(r!=DOK_STATUS_OK)return leave(s,r);
        }else if(category==1000){
            if(score==s->_world->category99_total)
                emit(s,DOK_INTERACTION_FINISH,seed->reference,hero,category,value,0);
        }else emit(s,DOK_INTERACTION_STAT,seed->reference,hero,category,value,0);
    }
    if(o->class_id==13u&&!same(s->previous_floor,seed->reference)){
        uint8_t glyph=(uint8_t)field(o,0x66,0);uint16_t target=s->_world->glyph_index[glyph];
        if(target==0u||target>s->_world->object_count)return leave(s,DOK_STATUS_INVALID_STATE);
        bits=(uint32_t)field(o,0x67,0);
        r=event(s,s->_world->objects[target-1u].reference,seed->reference,&bits,0,c2);
        if(r!=DOK_STATUS_OK)return leave(s,r);
    }
    s->previous_floor=seed->reference;return leave(s,DOK_STATUS_OK);
}
dok_status dok_interactions_callback(dok_interactions_state *s,dok_object_ref owner,uint16_t callback)
{
    const dok_world_object_seed *seed;const dok_level_object *o;dok_interaction_door *d;
    uint16_t flags;dok_status r=enter(s);dok_object_ref none={0,0,0};
    if(r!=DOK_STATUS_OK)return r;
    seed=find(s,owner);
    if(seed==NULL)return leave(s,DOK_STATUS_STALE_REFERENCE);
    o=definition(s,seed);
    if(o->class_id!=7u)return leave(s,DOK_STATUS_INVALID_ARGUMENT);
    if(callback!=DOK_INTERACTION_OPEN&&callback!=DOK_INTERACTION_CLOSE&&callback!=DOK_INTERACTION_AUTO)
        return leave(s,DOK_STATUS_UNSUPPORTED);
    if(s->_scheduler->_in_callback==0u)return leave(s,DOK_STATUS_INVALID_STATE);
    d=&s->doors[o->glyph];flags=s->_collision->flags[o->glyph];
    if(callback==DOK_INTERACTION_AUTO){
        if(word(s->_collision->door_count[o->glyph])>0||d->state!=1u||d->ready==0u)
            return leave(s,DOK_STATUS_OK);
        r=schedule(s,owner,DOK_INTERACTION_CLOSE,(uint32_t)field(o,0x66,0));
        if(r==DOK_STATUS_OK)r=dok_events_cancel_current(s->_scheduler);
        return leave(s,r);
    }
    if(callback==DOK_INTERACTION_CLOSE&&word(s->_collision->door_count[o->glyph])>0)
        return leave(s,DOK_STATUS_OK);
    ++s->revision;
    if(callback==DOK_INTERACTION_OPEN&&d->state==2u&&d->phase<word((uint32_t)d->extent-1u)){
        flags|=8u;d->ready=0;
        r=dok_collision_set_flags(s->_collision,owner,flags);
        if(r!=DOK_STATUS_OK)return leave(s,r);
        r=dok_events_set_interval(s->_scheduler,(uint32_t)(int32_t)word((uint32_t)field(o,0x52,0)));
        if(r!=DOK_STATUS_OK)return leave(s,r);
        emit(s,DOK_INTERACTION_SOUND_ENSURE,owner,none,0,0,0);
        d->phase=word((uint32_t)d->phase+1u);
        if(d->phase>d->extent/4){flags|=0x10u;
            r=dok_collision_set_flags(s->_collision,owner,flags);}
        return leave(s,r);
    }
    if(callback==DOK_INTERACTION_CLOSE&&d->state==1u&&d->phase>0){
        d->phase=word((uint32_t)d->phase-1u);d->ready=0;
        r=dok_events_set_interval(s->_scheduler,(uint32_t)(int32_t)word((uint32_t)field(o,0x52,0)));
        if(r!=DOK_STATUS_OK)return leave(s,r);
        emit(s,DOK_INTERACTION_SOUND_ENSURE,owner,none,0,0,0);
        return leave(s,DOK_STATUS_OK);
    }
    if(callback==DOK_INTERACTION_OPEN){d->state=1;flags|=0x10u;}
    else{d->state=2;d->phase=0;flags=o->flags;}
    r=dok_collision_set_flags(s->_collision,owner,flags);
    if(r!=DOK_STATUS_OK)return leave(s,r);
    emit(s,DOK_INTERACTION_SOUND_STOP,owner,none,0,0,0);
    d->marker=(uint8_t)~d->marker;
    emit(s,DOK_INTERACTION_STATUS,owner,none,0,(int16_t)d->state,0);
    d->ready=1;r=dok_events_cancel_current(s->_scheduler);return leave(s,r);
}
dok_status dok_interactions_phases(const dok_interactions_state *s,dok_interaction_phase_view *out)
{
    uint32_t i;dok_status r;
    if(out==NULL)return DOK_STATUS_INVALID_ARGUMENT;
    r=valid(s);if(r!=DOK_STATUS_OK)return r;
    memset(out,0,sizeof(*out));out->world_epoch=s->_epoch;out->revision=s->revision;
    for(i=0;i<256u;++i){out->phase[i]=s->doors[i].phase;out->extent[i]=s->doors[i].extent;}
    return DOK_STATUS_OK;
}
