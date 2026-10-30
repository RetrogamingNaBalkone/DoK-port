#include "dok/ai.h"
#include "dok/cell_math.h"
#include <limits.h>
#include <stddef.h>
#include <string.h>
#define TAG UINT32_C(0x41494e31)
#define OWNER UINT32_C(0x30000)
static const unsigned char angles[101]={
#include "ai_atan.inc"
};
static int16_t word(int64_t n)
{uint32_t u=(uint32_t)(uint64_t)n&65535u;return u<=32767u?(int16_t)u:(int16_t)((int32_t)u-65536);}
static int32_t wrap(int64_t n)
{uint32_t u=(uint32_t)(uint64_t)n;return u<=INT32_MAX?(int32_t)u:-1-(int32_t)(UINT32_MAX-u);}
static int32_t sar(int32_t n,uint32_t b)
{int64_t d=INT64_C(1)<<b;return n>=0?(int32_t)(n/d):-(int32_t)((-(int64_t)n+d-1)/d);}
static uint32_t same(dok_object_ref a,dok_object_ref b)
{return a.world_epoch==b.world_epoch&&a.object_id==b.object_id&&a.generation==b.generation;}
static dok_status valid(const dok_ai *s)
{
    if(!s||s->tag!=TAG||!s->world||s->busy||s->collision->_active)return DOK_STATUS_INVALID_STATE;
    return s->world->epoch==s->epoch?DOK_STATUS_OK:DOK_STATUS_STALE_REFERENCE;
}
static const dok_level_object *definition(const dok_ai *s,uint32_t glyph)
{uint32_t n=s->world->glyph_index[glyph];return n?&s->world->definition->objects[n-1u]:NULL;}
static int32_t field(const dok_level_object *o,uint16_t off)
{const dok_level_field *f=dok_level_field_at(o,off);return f?f->value:0;}
static dok_enemy *find(dok_ai *s,dok_object_ref ref)
{
    uint32_t i;for(i=0;i<s->world->object_count;++i)if(same(s->world->objects[i].reference,ref)){
        uint32_t glyph=s->world->definition->objects[i].glyph,n=s->glyph_index[glyph];return n?&s->enemies[n-1u]:NULL;}
    return NULL;
}
static dok_object_ref ref(const dok_ai *s,const dok_enemy *e)
{return s->world->objects[s->world->glyph_index[e->glyph]-1u].reference;}
static dok_object_ref service(const dok_ai *s,const dok_enemy *e)
{dok_object_ref r={s->epoch,OWNER+e->glyph,1};return r;}
static void emit(dok_ai *s,dok_enemy *e,uint32_t kind)
{dok_ai_output o={ref(s,e),kind,e->state,e->health};if(s->services.sink)s->services.sink(s->services.context,&o);}
static dok_status ticks(dok_ai *s,uint32_t *out)
{return s->scheduler->_config.read_ticks(s->scheduler->_config.clock_context,out);}
static const dok_sprite_animation *animation(const dok_ai *s,const dok_enemy *e,uint16_t kind,dok_asset_id *id)
{
    dok_presentation_value v;if(dok_presentation_get(s->presentation,e->glyph,kind,&v)!=DOK_BALANCE_OK)return NULL;
    if(id)*id=v.asset_id;
    for(uint32_t i=0;i<s->bank->animation_count;++i)if(s->bank->animations[i]==v.asset_id)return &s->bank->sequences[i];return NULL;
}
dok_status dok_ai_heading(int32_t dx,int32_t dz,int16_t *out)
{
    uint32_t x,z,index;int32_t angle;
    if(!out)return DOK_STATUS_INVALID_ARGUMENT;
    if(dx==INT32_MIN||dz==INT32_MIN)return DOK_STATUS_LIMIT_REACHED;
    x=(uint32_t)(dx<0?-dx:dx);z=(uint32_t)(dz<0?-dz:dz);
    if(x>INT32_MAX/100u||z>INT32_MAX/100u)return DOK_STATUS_LIMIT_REACHED;
    if(x<z){index=x*100u/z;angle=angles[index];
        if(dz>0&&dx>=0){}else if(dz<0&&dx>=0)angle=180-angle;
        else if(dz>0&&dx<0)angle=360-angle;else if(dz<0&&dx<0)angle+=180;else angle=0;
    }else if(x){index=z*100u/x;angle=angles[index];
        if(dx>0&&dz>=0)angle=90-angle;else if(dx<0&&dz>=0)angle+=270;
        else if(dx>0&&dz<0)angle+=90;else if(dx<0&&dz<0)angle=270-angle;else angle=0;
    }else angle=0;
    *out=(int16_t)((360-angle)%360);return DOK_STATUS_OK;
}
uint32_t dok_ai_near(int32_t dx,int32_t dz,int32_t activation,int16_t state)
{int64_t x=dx<0?-(int64_t)dx:dx,z=dz<0?-(int64_t)dz:dz;return state==0&&(x>z?x:z)<activation;}
uint32_t dok_ai_course_step(int16_t *counter,int16_t period)
{if(!counter)return 0;if(*counter==period){*counter=0;return 1;}*counter=word((int32_t)*counter+1);return 0;}
uint32_t dok_ai_burst_due(uint32_t elapsed,int16_t delay)
{return elapsed/10000u>(uint32_t)(int32_t)delay;}
int16_t dok_ai_damage_word(int16_t health,int16_t damage)
{int16_t n;if(!health)return 0;n=word((int32_t)health-damage);return n<0?0:n;}
static dok_status wake(dok_ai *s,dok_enemy *e)
{
    dok_status r;uint32_t t;if(e->state)return DOK_STATUS_OK;
    if(!s->registered)return DOK_STATUS_INVALID_STATE;
    if((r=ticks(s,&t))!=DOK_STATUS_OK)return r;
    e->state=1;e->next_cursor=0;e->shots=0;e->burst_started=t;
    e->course_counter=word(field(definition(s,e->glyph),0xae));
    r=dok_events_schedule(s->scheduler,service(s,e),DOK_AI_CALLBACK,e->glyph,416);
    if(r==DOK_STATUS_OK){++s->active;++s->revision;emit(s,e,DOK_AI_WAKE);}return r;
}
dok_status dok_ai_bind(dok_ai *s,dok_initial_world *w,dok_collision_state *collision,dok_event_scheduler *scheduler,
    const dok_level_presentation *presentation,dok_sprite_asset_bank *bank,dok_combat *combat,const dok_ai_services *services)
{
    if(!s||!w||!collision||!scheduler||!presentation||!bank||!combat||!services||!services->trig)return DOK_STATUS_INVALID_ARGUMENT;
    if(collision->_world!=w||collision->_active||scheduler->_dispatching||scheduler->_config.world_epoch!=w->epoch||presentation->effective!=w->definition||
       !w->hero.object_id||w->hero.object_id>w->object_count||!same(w->objects[w->hero.object_id-1u].reference,w->hero)||w->objects[w->hero.object_id-1u].class_id!=1u)return DOK_STATUS_INVALID_STATE;
    memset(s,0,sizeof(*s));s->world=w;s->collision=collision;s->scheduler=scheduler;s->presentation=presentation;s->bank=bank;s->combat=combat;
    s->services=*services;s->epoch=w->epoch;s->tag=TAG;s->revision=1;
    for(uint32_t i=0;i<w->object_count;++i)if(w->objects[i].class_id==9u){
        const dok_level_object *o=&w->definition->objects[i];const dok_world_object_seed *seed=&w->objects[i];dok_enemy *e;
        if(s->count==DOK_AI_LIMIT)return DOK_STATUS_LIMIT_REACHED;
        e=&s->enemies[s->count++];s->glyph_index[o->glyph]=(uint16_t)s->count;e->glyph=o->glyph;
        e->health=word(field(o,0xa4));e->displayed_cursor=0;
        if(dok_motion_init(&e->motion,seed->x,seed->y,seed->z,seed->heading,word(field(o,0x34)),field(o,0x38))!=DOK_STATUS_OK)return DOK_STATUS_INVALID_STATE;
    }
    return DOK_STATUS_OK;
}
dok_status dok_ai_register(dok_ai *s)
{
    dok_status r=valid(s);if(r!=DOK_STATUS_OK)return r;if(s->registered||s->scheduler->_dispatching)return DOK_STATUS_INVALID_STATE;
    for(uint32_t i=0;i<s->count;++i)if((r=dok_events_register(s->scheduler,service(s,&s->enemies[i]),4))!=DOK_STATUS_OK)return r;
    s->registered=1;return DOK_STATUS_OK;
}
static dok_status consider(dok_ai *s,dok_enemy *e,int32_t hx,int32_t hz)
{
    const dok_world_object_seed *seed=&s->world->objects[s->world->glyph_index[e->glyph]-1u];
    if(!seed->pose_known)return DOK_STATUS_OK;
    ++s->considerations;
    if(dok_ai_near(wrap((int64_t)hx-e->motion.x),wrap((int64_t)hz-e->motion.z),field(definition(s,e->glyph),0xba),e->state))return wake(s,e);
    return DOK_STATUS_OK;
}
dok_status dok_ai_consider(dok_ai *s,dok_object_ref target,int32_t hx,int32_t hz)
{
    dok_enemy *e;dok_status r=valid(s);if(r!=DOK_STATUS_OK)return r;e=find(s,target);if(!e)return DOK_STATUS_STALE_REFERENCE;
    s->busy=1;r=consider(s,e,hx,hz);s->busy=0;return r;
}
dok_status dok_ai_consider_camera(dok_ai *s,const dok_nds_render_camera *camera,uint32_t heading)
{
    dok_render_gather_bounds bounds;dok_status r=valid(s);if(r!=DOK_STATUS_OK)return r;
    if(!camera||dok_render_bounds(s->world,camera,heading,&bounds)!=DOK_RENDER_SCENE_OK)return DOK_STATUS_INVALID_ARGUMENT;
    const dok_world_object_seed *hero=&s->world->objects[s->world->hero.object_id-1u];s->busy=1;
    for(int32_t z=bounds.min_z;z<=bounds.max_z;++z)for(int32_t x=bounds.min_x;x<=bounds.max_x;++x){
        uint32_t cell=(uint32_t)z*(uint16_t)s->world->definition->settings[0]+(uint32_t)x;
        const dok_level_object *tile=definition(s,s->world->maps[DOK_WORLD_TMAP][cell]);
        if(!tile){r=DOK_STATUS_INVALID_STATE;goto done;}if(tile->class_id==2u)continue;
        for(uint32_t plane=0;plane<2u;++plane){uint32_t glyph=s->world->maps[plane][cell],n=s->glyph_index[glyph];
            if(n&&(r=consider(s,&s->enemies[n-1u],hero->x,hero->z))!=DOK_STATUS_OK)goto done;}
    }
done:s->busy=0;return r;
}
dok_status dok_ai_hit(dok_ai *s,dok_object_ref target,dok_object_ref source,int16_t damage,uint32_t *accepted)
{
    dok_enemy *e;dok_status r=valid(s);if(r!=DOK_STATUS_OK)return r;if(!accepted)return DOK_STATUS_INVALID_ARGUMENT;
    if(source.world_epoch!=s->epoch)return DOK_STATUS_STALE_REFERENCE;e=find(s,target);if(!e)return DOK_STATUS_STALE_REFERENCE;
    s->busy=1;*accepted=e->corpse==0u&&e->health!=0;
    if(*accepted){if(!e->state)r=wake(s,e);else {e->health=dok_ai_damage_word(e->health,damage);e->state=3;++s->revision;emit(s,e,DOK_AI_HURT);}}
    s->busy=0;return r;
}
static uint32_t index_at(const dok_ai *s,int32_t x,int32_t z)
{
    const dok_level_definition *d=s->world->definition;int32_t cx=word(dok_cell_quotient(x,d->settings[3])),cz=word(dok_cell_quotient(z,d->settings[3]));
    if(cx<0)cx=0;if(cz<0)cz=0;if(cx>=d->settings[0])cx=d->settings[0]-1;if(cz>=d->settings[2])cz=d->settings[2]-1;
    return (uint32_t)cz*(uint16_t)d->settings[0]+(uint32_t)cx;
}
static dok_status advance(dok_ai *s,int32_t *x,int32_t *z,int16_t h,int16_t distance)
{
    int32_t sn,cs;if(h<0||h>360||!s->services.trig(s->services.context,(uint16_t)h,&sn,&cs))return DOK_STATUS_INVALID_STATE;
    *x=wrap((int64_t)*x+sar(wrap((int64_t)sn*distance),15));*z=wrap((int64_t)*z-sar(wrap((int64_t)cs*distance),15));return DOK_STATUS_OK;
}
static const dok_level_object *at(const dok_ai *s,uint32_t plane,int32_t x,int32_t z)
{
    uint32_t cell=index_at(s,x,z),glyph=s->world->maps[plane][cell];
    if(plane==DOK_WORLD_MMAP&&s->combat&&(s->combat->occupied[cell/8u]&(1u<<(cell%8u))))
        for(uint32_t i=0;i<256u;++i)if(s->combat->projectiles[i].occupied&&s->combat->projectiles[i].cell==cell){glyph=s->combat->projectiles[i].template_glyph;break;}
    return definition(s,glyph);
}
static dok_status front(dok_ai *s,dok_enemy *e,const dok_level_object **out)
{
    int32_t x=e->motion.x,z=e->motion.z;int16_t distance=word(-s->world->definition->settings[3]);dok_status r;
    if((r=advance(s,&x,&z,e->motion.heading,distance))!=DOK_STATUS_OK)return r;
    *out=at(s,DOK_WORLD_MMAP,x,z);if(!*out)return DOK_STATUS_INVALID_STATE;
    if((*out)->class_id==0u){if((r=advance(s,&x,&z,e->motion.heading,distance))!=DOK_STATUS_OK)return r;*out=at(s,DOK_WORLD_TMAP,x,z);}
    return *out?DOK_STATUS_OK:DOK_STATUS_INVALID_STATE;
}
static dok_status sight(dok_ai *s,dok_enemy *e,uint32_t *visible)
{
    int32_t x=e->motion.x,z=e->motion.z;dok_status r;*visible=0;
    uint32_t hero_glyph=s->world->definition->objects[s->world->hero.object_id-1u].glyph;
    for(uint32_t i=0;i<5000u;++i){const dok_level_object *o;
        if((r=advance(s,&x,&z,e->motion.heading,-100))!=DOK_STATUS_OK)return r;
        o=at(s,DOK_WORLD_MMAP,x,z);if(!o)return DOK_STATUS_INVALID_STATE;
        /* Native compares identity with self before target/flags and skips
         * the second advancing lookup for a repeated self glyph. */
        if(o->glyph==e->glyph)continue;
        if(o->glyph==hero_glyph){*visible=1;return DOK_STATUS_OK;}
        if(!(s->collision->flags[o->glyph]&0x18u))return DOK_STATUS_OK;
        if((r=advance(s,&x,&z,e->motion.heading,-100))!=DOK_STATUS_OK)return r;
        o=at(s,DOK_WORLD_TMAP,x,z);if(!o)return DOK_STATUS_INVALID_STATE;
        if(!(s->collision->flags[o->glyph]&0x18u))return DOK_STATUS_OK;
    }return DOK_STATUS_OK;
}
dok_status dok_ai_sight(dok_ai *s,dok_object_ref target,uint32_t *visible)
{
    dok_status r=valid(s);dok_enemy *e;if(r!=DOK_STATUS_OK)return r;if(!visible)return DOK_STATUS_INVALID_ARGUMENT;
    e=find(s,target);if(!e)return DOK_STATUS_STALE_REFERENCE;
    uint32_t index=index_at(s,e->motion.x,e->motion.z);unsigned char old=s->world->maps[DOK_WORLD_MMAP][index];
    if(old!=e->glyph&&old!=' '&&!dok_collision_respawn_origin(s->collision,ref(s,e),index))return DOK_STATUS_INVALID_STATE;
    s->busy=1;s->world->maps[DOK_WORLD_MMAP][index]=' ';r=sight(s,e,visible);
    s->world->maps[DOK_WORLD_MMAP][index]=old;s->busy=0;return r;
}
static dok_status course(dok_ai *s,dok_enemy *e,int16_t old_heading)
{
    uint32_t origin=index_at(s,e->motion.x,e->motion.z);unsigned char old=s->world->maps[DOK_WORLD_MMAP][origin];dok_status r;
    const dok_world_object_seed *hero=&s->world->objects[s->world->hero.object_id-1u];dok_collision_view view;
    uint32_t overlap=dok_collision_respawn_origin(s->collision,ref(s,e),origin);
    if(old!=e->glyph&&old!=' '&&!overlap)return DOK_STATUS_INVALID_STATE;
    if((r=dok_ai_heading(wrap((int64_t)hero->x-e->motion.x),wrap((int64_t)hero->z-e->motion.z),&e->motion.heading))!=DOK_STATUS_OK)return r;
    if(!dok_ai_course_step(&e->course_counter,word(field(definition(s,e->glyph),0xae)))){e->motion.heading=old_heading;return DOK_STATUS_OK;}
    int32_t x=e->motion.x,z=e->motion.z;int16_t half=word(-sar(s->world->definition->settings[3],1));
    s->world->maps[DOK_WORLD_MMAP][origin]=' ';
    r=dok_collision_get_view(s->collision,&view);
    for(uint32_t i=0;r==DOK_STATUS_OK&&i<10u;++i){int32_t nx=x,nz=z,ax,az;
        r=advance(s,&nx,&nz,e->motion.heading,half);if(r!=DOK_STATUS_OK)break;
        r=dok_collision_native_resolve(&view,x,z,nx,nz,&ax,&az);if(r!=DOK_STATUS_OK)break;
        x=ax;z=az;++s->course_trials;if(index_at(s,x,z)!=origin)break;}
    s->world->maps[DOK_WORLD_MMAP][origin]=overlap?e->glyph:old;
    if(r!=DOK_STATUS_OK)return r;
    r=dok_ai_heading(wrap((int64_t)x-e->motion.x),wrap((int64_t)z-e->motion.z),&e->motion.heading);
    if(r==DOK_STATUS_OK)emit(s,e,DOK_AI_COURSE);return r;
}
static dok_status walking(dok_ai *s,dok_enemy *e,uint32_t now,const dok_level_object *front_object)
{
    const dok_sprite_animation *a;dok_asset_id id;dok_sprite_frame frame;dok_status r;int16_t old=e->motion.heading;
    dok_collision_context context;dok_collision_report collision_report;dok_motion_report motion_report;
    uint32_t origin=index_at(s,e->motion.x,e->motion.z);unsigned char old_cell=s->world->maps[DOK_WORLD_MMAP][origin];
    const dok_world_object_seed *hero=&s->world->objects[s->world->hero.object_id-1u];
    uint32_t overlap=dok_collision_respawn_origin(s->collision,ref(s,e),origin);
    if(old_cell!=e->glyph&&old_cell!=' '&&!overlap)return DOK_STATUS_INVALID_STATE;
    s->world->maps[DOK_WORLD_MMAP][origin]=' ';
    r=dok_ai_heading(wrap((int64_t)hero->x-e->motion.x),wrap((int64_t)hero->z-e->motion.z),&e->motion.heading);
    if(r==DOK_STATUS_OK)r=ticks(s,&now);
    if(r==DOK_STATUS_OK&&dok_ai_burst_due(now-e->burst_started,word(field(definition(s,e->glyph),0xa8)))){
        uint32_t visible=0;
        if(e->shots<word(field(definition(s,e->glyph),0xa6))&&front_object->class_id!=8u&&front_object->class_id!=9u)r=sight(s,e,&visible);
        if(r==DOK_STATUS_OK){
            if(visible){dok_object_ref instance;e->shots=word((int32_t)e->shots+1);e->course_counter=word(field(definition(s,e->glyph),0xae));
                r=dok_combat_spawn(s->combat,ref(s,e),(uint8_t)field(definition(s,e->glyph),0xe7),e->motion.x,e->motion.z,e->motion.heading,&instance);
                if(r==DOK_STATUS_NO_SPACE)r=DOK_STATUS_OK;emit(s,e,DOK_AI_RANGED);
            }else {r=ticks(s,&e->burst_started);e->shots=0;}
        }
    }
    if(r==DOK_STATUS_OK)r=course(s,e,old);
    s->world->maps[DOK_WORLD_MMAP][origin]=overlap?e->glyph:old_cell;
    if(r!=DOK_STATUS_OK)return r;
    a=animation(s,e,DOK_PRESENT_ANIMATION,&id);if(!a)return DOK_STATUS_INVALID_STATE;
    if(e->next_cursor<0||(uint32_t)e->next_cursor>=a->count)e->next_cursor=0;
    e->displayed_cursor=e->next_cursor;e->next_cursor=word((int32_t)e->next_cursor+1);
    if(dok_sprite_animation_get(a,e->displayed_cursor,&frame)!=DOK_STATUS_OK||dok_sprite_asset_select(s->bank,e->glyph,id,e->displayed_cursor)!=DOK_STATUS_OK)return DOK_STATUS_INVALID_STATE;
    e->displayed_kind=0;e->motion.flags=DOK_INPUT_NATIVE_FORWARD;
    if((r=ticks(s,&now))!=DOK_STATUS_OK)return r;
    uint32_t elapsed=e->motion_started?now-e->last_motion_ticks:0;
    if((r=dok_collision_begin(s->collision,ref(s,e),&e->motion,elapsed,&context))!=DOK_STATUS_OK)return r;
    dok_motion_services services={s->services.context,s->services.trig,&context,dok_collision_move};
    r=dok_motion_update(&e->motion,elapsed,&services,&motion_report);
    dok_status end=dok_collision_end(&context,&collision_report);if(r!=DOK_STATUS_OK)return r;if(end!=DOK_STATUS_OK)return end;
    dok_world_object_seed *seed=&s->world->objects[s->world->glyph_index[e->glyph]-1u];
    seed->x=e->motion.x;seed->y=e->motion.y;seed->z=e->motion.z;seed->heading=e->motion.heading;
    if((r=ticks(s,&e->last_motion_ticks))!=DOK_STATUS_OK)return r;
    e->motion_started=1;++s->motion_callbacks;emit(s,e,DOK_AI_MOVED);
    return dok_events_set_interval(s->scheduler,(uint32_t)(int32_t)frame.interval);
}
static dok_status publish(dok_ai *s,dok_enemy *e,uint16_t kind,int16_t cursor,dok_sprite_frame *frame)
{
    dok_asset_id id;const dok_sprite_animation *a=animation(s,e,kind,&id);
    if(!a||dok_sprite_animation_get(a,cursor,frame)!=DOK_STATUS_OK||dok_sprite_asset_select(s->bank,e->glyph,id,cursor)!=DOK_STATUS_OK)return DOK_STATUS_INVALID_STATE;
    e->displayed_cursor=cursor;e->displayed_kind=kind==0xec?2u:kind==0xe8?1u:0u;return DOK_STATUS_OK;
}
static dok_status attacking(dok_ai *s,dok_enemy *e)
{
    dok_sprite_frame frame;dok_status r;const dok_sprite_animation *a=animation(s,e,0xe8,NULL);
    if(!a)return DOK_STATUS_INVALID_STATE;emit(s,e,DOK_AI_CONTACT);
    if(e->state==1u){e->next_cursor=0;e->state=2;emit(s,e,DOK_AI_ATTACK);}
    if(e->next_cursor<0)e->next_cursor=0;
    if((uint32_t)e->next_cursor>=a->count){uint32_t accepted;
        r=dok_combat_hurt_hero(s->combat,ref(s,e),5,&accepted);if(r!=DOK_STATUS_OK)return r;
        emit(s,e,DOK_AI_MELEE);e->next_cursor=0;
    }
    r=publish(s,e,0xe8,e->next_cursor,&frame);if(r!=DOK_STATUS_OK)return r;
    e->next_cursor=word((int32_t)e->next_cursor+1);
    if(e->state==3u){r=publish(s,e,0xec,0,&frame);if(r!=DOK_STATUS_OK)return r;e->state=2;}
    return dok_events_set_interval(s->scheduler,(uint32_t)(int32_t)frame.interval);
}
static dok_status dying(dok_ai *s,dok_enemy *e)
{
    dok_status r;dok_sprite_frame frame;const dok_sprite_animation *a=animation(s,e,0xec,NULL);
    if(!a)return DOK_STATUS_INVALID_STATE;
    if(e->corpse)return dok_events_cancel_current(s->scheduler);
    if(e->state!=4u){e->state=4;e->next_cursor=0;emit(s,e,DOK_AI_DYING);}
    if(e->next_cursor>=0&&(uint32_t)e->next_cursor<a->count){
        r=publish(s,e,0xec,e->next_cursor,&frame);if(r!=DOK_STATUS_OK)return r;
        e->next_cursor=word((int32_t)e->next_cursor+1);
        return dok_events_set_interval(s->scheduler,(uint32_t)(int32_t)frame.interval);
    }
    uint32_t index=index_at(s,e->motion.x,e->motion.z);
    unsigned char current=s->world->maps[DOK_WORLD_MMAP][index];
    const dok_level_object *tile=definition(s,s->world->maps[DOK_WORLD_TMAP][index]);
    if(!tile||(current!=e->glyph&&current!=' '&&!dok_collision_respawn_origin(s->collision,ref(s,e),index)))return DOK_STATUS_INVALID_STATE;
    s->world->maps[DOK_WORLD_MMAP][index]=' ';
    s->world->maps[tile->class_id==0u?DOK_WORLD_TMAP:DOK_WORLD_MMAP][index]=e->glyph;
    r=dok_collision_set_flags(s->collision,ref(s,e),(uint16_t)(s->collision->flags[e->glyph]|0x10u));if(r!=DOK_STATUS_OK)return r;
    e->corpse=1;s->corpses[e->glyph/8u]|=(unsigned char)(1u<<(e->glyph%8u));if(s->active)--s->active;
    emit(s,e,DOK_AI_DEAD);
    /* Corpse retains its source slot and owner identity. Its already fired
     * projectiles remain legal; world clear/removal owns their destruction. */
    return dok_events_cancel_current(s->scheduler);
}
dok_status dok_ai_callback(dok_ai *s,dok_object_ref owner,uint16_t kind)
{
    dok_status r=valid(s);uint32_t n,t;if(r!=DOK_STATUS_OK)return r;
    if(kind!=DOK_AI_CALLBACK||owner.world_epoch!=s->epoch||owner.generation!=1||owner.object_id<OWNER||owner.object_id>=OWNER+256u)return DOK_STATUS_INVALID_ARGUMENT;
    if(!s->scheduler->_in_callback||s->scheduler->_current>=DOK_EVENT_OWNER_LIMIT||!same(s->scheduler->_bank->owners[s->scheduler->_current].reference,owner))return DOK_STATUS_INVALID_STATE;
    n=s->glyph_index[owner.object_id-OWNER];if(!n)return DOK_STATUS_STALE_REFERENCE;
    dok_enemy *e=&s->enemies[n-1u];if((r=ticks(s,&t))!=DOK_STATUS_OK)return r;s->busy=1;++s->callbacks;
    if(e->health<=0)r=dying(s,e);
    else {const dok_level_object *f;r=front(s,e,&f);
        if(r==DOK_STATUS_OK){if(f->class_id==1u)r=attacking(s,e);
            else {if(e->state==3u){dok_sprite_frame frame;r=publish(s,e,0xec,0,&frame);}
                e->state=1;if(r==DOK_STATUS_OK)r=walking(s,e,t,f);}}}
    s->busy=0;if(r!=DOK_STATUS_OK)s->fault=(uint32_t)r;else ++s->revision;return r;
}
dok_status dok_ai_clear(dok_ai *s)
{
    dok_status r=valid(s);if(r!=DOK_STATUS_OK)return r;if(s->scheduler->_dispatching)return DOK_STATUS_INVALID_STATE;
    if(s->registered)for(uint32_t i=0;i<s->count;++i)if((r=dok_events_destroy(s->scheduler,service(s,&s->enemies[i])))!=DOK_STATUS_OK)return r;
    s->registered=s->active=s->tag=0;return DOK_STATUS_OK;
}
