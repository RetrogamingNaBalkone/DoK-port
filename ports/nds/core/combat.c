#include "dok/combat.h"
#include <limits.h>
#include <stddef.h>
#include <string.h>
#define TAG UINT32_C(0x434d4231)
#define POOL UINT32_C(0x10000)
#define WEAPON UINT32_C(0x20000)
static int16_t word(int64_t n)
{uint32_t u=(uint32_t)(uint64_t)n&65535u;return u<=32767u?(int16_t)u:(int16_t)((int32_t)u-65536);}
static int32_t wrap(int64_t n)
{uint32_t u=(uint32_t)(uint64_t)n;return u<=INT32_MAX?(int32_t)u:-1-(int32_t)(UINT32_MAX-u);}
static int32_t sar(int32_t n,uint32_t b)
{int64_t d=INT64_C(1)<<b;return n>=0?(int32_t)(n/d):-(int32_t)((-(int64_t)n+d-1)/d);}
static uint32_t same(dok_object_ref a,dok_object_ref b)
{return a.world_epoch==b.world_epoch&&a.object_id==b.object_id&&a.generation==b.generation;}
static dok_object_ref service(const dok_combat *s,uint32_t id)
{dok_object_ref r={s->epoch,id,1};return r;}
static dok_object_ref reference(const dok_combat *s,uint32_t slot)
{dok_object_ref r={s->epoch,257u+slot,s->projectiles[slot].generation};return r;}
static dok_status valid(const dok_combat *s)
{
    if(!s||s->tag!=TAG||!s->items||!s->scheduler||s->busy)return DOK_STATUS_INVALID_STATE;
    if(s->epoch!=s->items->world->epoch)return DOK_STATUS_STALE_REFERENCE;
    if(s->items->collision->_active)return DOK_STATUS_INVALID_STATE;
    return DOK_STATUS_OK;
}
static const dok_level_object *object(const dok_combat *s,uint32_t glyph)
{uint32_t n=s->items->world->glyph_index[glyph];return n?&s->items->world->definition->objects[n-1u]:NULL;}
static const dok_world_object_seed *seed(const dok_combat *s,dok_object_ref ref)
{uint32_t i;for(i=0;i<s->items->world->object_count;++i)if(same(s->items->world->objects[i].reference,ref))return &s->items->world->objects[i];return NULL;}
static int32_t field(const dok_level_object *o,uint16_t off)
{const dok_level_field *f=dok_level_field_at(o,off);return f?f->value:0;}
static dok_status now(dok_combat *s,uint32_t *out)
{return s->scheduler->_config.read_ticks(s->scheduler->_config.clock_context,out);}
static void emit(dok_combat *s,uint32_t kind,dok_object_ref instance,dok_object_ref source,
    dok_object_ref target,int16_t damage,uint16_t glyph)
{
    dok_combat_output o={instance,source,target,kind,damage,glyph};
    if(s->services.sink)s->services.sink(s->services.context,&o);
}
static const dok_sprite_animation *animation(const dok_combat *s,uint8_t glyph,uint16_t kind)
{
    dok_presentation_value v;uint32_t i;
    if(dok_presentation_get(s->presentation,glyph,kind,&v)!=DOK_BALANCE_OK)return NULL;
    for(i=0;i<s->bank->animation_count;++i)if(s->bank->animations[i]==v.asset_id)return &s->bank->sequences[i];
    return NULL;
}
static uint32_t cell(const dok_combat *s,int32_t x,int32_t z)
{
    const dok_level_definition *d=s->items->world->definition;int32_t cx=word(x/d->settings[3]),cz=word(z/d->settings[3]);
    if(cx<0)cx=0;if(cz<0)cz=0;if(cx>=d->settings[0])cx=d->settings[0]-1;if(cz>=d->settings[2])cz=d->settings[2]-1;
    return (uint32_t)cz*(uint16_t)d->settings[0]+(uint32_t)cx;
}
static void release_cell(dok_combat *s,dok_projectile *p)
{if(p->occupied){s->occupied[p->cell/8u]&=(unsigned char)~(1u<<(p->cell%8u));p->occupied=0;}}
static void acquire_cell(dok_combat *s,dok_projectile *p)
{
    p->cell=(uint16_t)cell(s,p->x,p->z);
    if(s->items->world->maps[DOK_WORLD_MMAP][p->cell]==' '&&!(s->occupied[p->cell/8u]&(1u<<(p->cell%8u)))){
        s->occupied[p->cell/8u]|=(unsigned char)(1u<<(p->cell%8u));p->occupied=1;}
}
int16_t dok_projectile_decay(int16_t d,int16_t f)
{return word(sar(word((int32_t)d+f),1));}
dok_status dok_projectile_distance(int16_t speed,uint32_t elapsed,int16_t side,int32_t *distance,int16_t *quarter)
{
    uint32_t factor;if(!distance||!quarter||side<4)return DOK_STATUS_INVALID_ARGUMENT;
    factor=(elapsed<<8)/416u;if(factor<256u)factor=256u;if(factor>2048u)factor=2048u;
    *distance=sar(wrap((int64_t)speed*factor),8);*quarter=word(sar(-side,2));return DOK_STATUS_OK;
}
uint32_t dok_weapon_ready(dok_weapon_runtime *w)
{if(!w||(w->state!=7&&w->state!=3))return 0;w->state=2;return 1;}
dok_status dok_weapon_step(dok_weapon_runtime *w,uint32_t count,uint32_t *cancel)
{
    if(!w||!cancel||!count||count>32767u)return DOK_STATUS_INVALID_ARGUMENT;*cancel=0;
    if(w->state==1){w->shift=word((int32_t)w->shift-1);if(!w->shift)w->state|=4;return DOK_STATUS_OK;}
    if(w->state==0){w->shift=word((int32_t)w->shift+1);if(w->shift==3){w->state|=4;*cancel=1;}return DOK_STATUS_OK;}
    if(w->state==2){if(w->cursor>=(int32_t)count-1)w->state|=4;else w->cursor=word((int32_t)w->cursor+1);}
    else {w->state=3;if(!w->cursor)w->state|=4;else if(w->cursor>=(int32_t)count-1)w->cursor=0;else w->cursor=word((int32_t)w->cursor+1);}
    return DOK_STATUS_OK;
}
dok_status dok_combat_bind(dok_combat *s,dok_items *items,dok_event_scheduler *scheduler,
    const dok_level_presentation *presentation,const dok_sprite_asset_bank *bank,const dok_combat_services *services)
{
    if(!s||!items||!scheduler||!presentation||!bank||!services||!services->trig)return DOK_STATUS_INVALID_ARGUMENT;
    if(items->busy||items->collision->_active||scheduler->_dispatching||scheduler->_config.world_epoch!=items->epoch||presentation->effective!=items->world->definition)
        return DOK_STATUS_INVALID_STATE;
    memset(s,0,sizeof(*s));s->items=items;s->scheduler=scheduler;s->presentation=presentation;s->bank=bank;s->services=*services;
    s->epoch=items->epoch;s->tag=TAG;s->revision=1;s->executing=UINT32_MAX;
    for(uint32_t i=0;i<DOK_WEAPON_SLOTS;++i)s->weapons[i].state=4;
    return dok_collision_projectiles(items->collision,s->occupied);
}
dok_status dok_combat_register(dok_combat *s)
{
    dok_status r=valid(s);uint32_t i;if(r!=DOK_STATUS_OK)return r;
    if(s->registered||s->scheduler->_dispatching)return DOK_STATUS_INVALID_STATE;
    for(i=0;i<256u;++i){r=dok_events_register(s->scheduler,service(s,POOL+i),2);if(r!=DOK_STATUS_OK)return r;}
    for(i=0;i<=DOK_WEAPON_SLOTS;++i){r=dok_events_register(s->scheduler,service(s,WEAPON+i),2);if(r!=DOK_STATUS_OK)return r;}
    s->registered=1;return DOK_STATUS_OK;
}
static dok_status spawn(dok_combat *s,dok_object_ref owner,uint8_t glyph,int32_t x,int32_t z,int16_t heading,dok_object_ref *out)
{
    const dok_level_object *o=object(s,glyph);dok_projectile *p;uint32_t slot,t;dok_status r;const dok_event_owner *event_owner;
    if(!seed(s,owner))return DOK_STATUS_STALE_REFERENCE;
    if(!s->registered||!o||o->class_id!=8u||heading<0||heading>=360||!animation(s,glyph,DOK_PRESENT_ANIMATION)||!animation(s,glyph,0x77)||!animation(s,glyph,0x7b))return DOK_STATUS_INVALID_ARGUMENT;
    for(slot=0;slot<256u;++slot){
        const dok_level_object *old=object(s,slot);
        if(s->projectiles[slot].phase||slot==s->executing)continue;
        if(old&&!(s->items->collision->flags[slot]&0x400u))continue;
        if(s->projectiles[slot].generation==UINT32_MAX)continue;
        r=dok_events_inspect_owner(s->scheduler,service(s,POOL+slot),&event_owner);
        if(r!=DOK_STATUS_OK)return r;if(!event_owner->count)break;
    }
    if(slot==256u)return DOK_STATUS_NO_SPACE;
    if((r=now(s,&t))!=DOK_STATUS_OK)return r;
    p=&s->projectiles[slot];
    if(object(s,slot))emit(s,DOK_COMBAT_RECLAIM,s->items->world->objects[s->items->world->glyph_index[slot]-1u].reference,owner,(dok_object_ref){0},0,0);
    ++p->generation;p->owner=owner;p->x=x;p->z=z;p->y=-(int32_t)o->height_parameter;p->heading=heading;
    p->speed=word(-field(o,0x3c));p->damage=word(field(o,0x5c));p->damage_floor=word(sar(p->damage,1));
    p->cursor=0;p->phase=1;p->template_glyph=glyph;p->last_ticks=t;p->occupied=0;p->effect_hit=0;
    r=dok_events_schedule(s->scheduler,service(s,POOL+slot),DOK_COMBAT_PROJECTILE,p->generation,416);
    if(r!=DOK_STATUS_OK){p->phase=0;return r;}
    ++s->active;if(s->active>s->high_water)s->high_water=s->active;++s->revision;
    *out=reference(s,slot);emit(s,DOK_COMBAT_SHOT,*out,owner,(dok_object_ref){0},p->damage,glyph);return DOK_STATUS_OK;
}
dok_status dok_combat_spawn(dok_combat *s,dok_object_ref owner,uint8_t glyph,int32_t x,int32_t z,int16_t heading,dok_object_ref *out)
{
    dok_status r;if(!out)return DOK_STATUS_INVALID_ARGUMENT;r=valid(s);if(r!=DOK_STATUS_OK)return r;
    s->busy=1;r=spawn(s,owner,glyph,x,z,heading,out);s->busy=0;return r;
}
static dok_status show(dok_combat *s,uint32_t slot)
{
    dok_weapon_runtime *w=&s->weapons[slot];dok_sprite_frame f;const dok_sprite_animation *a;
    dok_object_ref ref=s->items->inventory.weapons[slot];const dok_world_object_seed *owner=seed(s,ref);
    if(!owner)return DOK_STATUS_STALE_REFERENCE;
    a=animation(s,s->items->world->definition->objects[owner->definition_index].glyph,DOK_PRESENT_ANIMATION);
    if(!a||dok_sprite_animation_get(a,0,&f)!=DOK_STATUS_OK)return DOK_STATUS_INVALID_STATE;
    if(w->state!=4)return DOK_STATUS_OK;
    w->state=1;w->shift=2;w->cursor=0;w->active=1;
    return dok_events_schedule(s->scheduler,service(s,WEAPON+slot),DOK_COMBAT_WEAPON,slot,(uint32_t)(int32_t)f.interval);
}
static dok_status controls(dok_combat *s,const dok_control_commands *input)
{
    dok_inventory *inv=&s->items->inventory;dok_status r;uint32_t changed,accepted;dok_object_ref instance;
    if(input->next_weapon_pressed){r=dok_items_next_weapon(s->items,&changed);if(r!=DOK_STATUS_OK)return r;}
    if(inv->selected>=DOK_WEAPON_SLOTS||inv->pending>=DOK_WEAPON_SLOTS)return DOK_STATUS_INVALID_STATE;
    if(!inv->weapons[inv->selected].object_id)return DOK_STATUS_OK;
    if(s->weapons[inv->selected].state==4&&!s->swap_active){r=show(s,inv->selected);if(r!=DOK_STATUS_OK)return r;}
    if(inv->selected!=inv->pending&&inv->weapons[inv->pending].object_id&&!s->swap_active&&
       (s->weapons[inv->selected].state&4)){
        dok_weapon_runtime *w=&s->weapons[inv->selected];w->state=0;w->shift=0;w->cursor=0;s->swap_active=1;
        r=dok_events_schedule(s->scheduler,service(s,WEAPON+DOK_WEAPON_SLOTS),DOK_COMBAT_SWITCH,0,416);if(r!=DOK_STATUS_OK)return r;
    }
    if((input->native_commands&DOK_INPUT_NATIVE_FIRE)&&inv->health>0&&inv->charge!=0&&dok_weapon_ready(&s->weapons[inv->selected])){
        const dok_world_object_seed *hero=seed(s,s->items->world->hero);
        if(!hero)return DOK_STATUS_STALE_REFERENCE;
        r=spawn(s,hero->reference,(uint8_t)('0'+inv->selected),hero->x,hero->z,hero->heading,&instance);
        if(r==DOK_STATUS_NO_SPACE)return DOK_STATUS_OK;
        if(r!=DOK_STATUS_OK)return r;
        return dok_items_stat(s->items,1,word(-(int32_t)s->projectiles[instance.object_id-257u].damage),&accepted);
    }
    if((input->native_commands&DOK_INPUT_NATIVE_FIRE)&&!inv->charge)emit(s,DOK_COMBAT_EMPTY,s->items->world->hero,(dok_object_ref){0},(dok_object_ref){0},0,0);
    return DOK_STATUS_OK;
}
dok_status dok_combat_controls(dok_combat *s,const dok_control_commands *input)
{
    dok_status r;if(!input)return DOK_STATUS_INVALID_ARGUMENT;r=valid(s);if(r!=DOK_STATUS_OK)return r;
    s->busy=1;r=controls(s,input);s->busy=0;return r;
}
static dok_status hurt_hero(dok_combat *s,dok_object_ref source,int16_t damage,uint16_t glyph)
{
    uint32_t accepted;dok_status r;
    if(!s->items->inventory.health)return DOK_STATUS_OK;
    r=dok_items_stat(s->items,0,word(-(int32_t)damage),&accepted);if(r!=DOK_STATUS_OK)return r;
    emit(s,s->items->inventory.health?DOK_COMBAT_DAMAGE:DOK_COMBAT_HERO_DEAD,s->items->world->hero,source,s->items->world->hero,damage,glyph);
    return DOK_STATUS_OK;
}
dok_status dok_combat_hurt_hero(dok_combat *s,dok_object_ref source,int16_t damage,uint32_t *accepted)
{
    dok_status r=valid(s);if(r!=DOK_STATUS_OK)return r;if(!accepted)return DOK_STATUS_INVALID_ARGUMENT;
    if(!seed(s,source))return DOK_STATUS_STALE_REFERENCE;
    s->busy=1;r=hurt_hero(s,source,damage,0);s->busy=0;if(r==DOK_STATUS_OK)*accepted=1;return r;
}
static dok_status contact(dok_combat *s,dok_projectile *p,uint32_t index,uint32_t *accepted,uint16_t *target_class,int16_t *height,dok_object_ref *target)
{
    const dok_level_object *o;const dok_world_object_seed *owner=seed(s,p->owner);uint32_t glyph=s->items->world->maps[DOK_WORLD_MMAP][index],i;
    if(!owner)return DOK_STATUS_STALE_REFERENCE;
    if(s->occupied[index/8u]&(1u<<(index%8u)))for(i=0;i<256u;++i)if(s->projectiles[i].occupied&&s->projectiles[i].cell==index){glyph=s->projectiles[i].template_glyph;break;}
    o=object(s,glyph);if(!o)return DOK_STATUS_INVALID_STATE;
    if(o->class_id==0u||o->class_id==8u){glyph=s->items->world->maps[DOK_WORLD_TMAP][index];o=object(s,glyph);if(!o)return DOK_STATUS_INVALID_STATE;}
    *accepted=0;*target_class=o->class_id;*height=o->height_parameter;
    *target=s->items->world->objects[s->items->world->glyph_index[glyph]-1u].reference;
    if(o->class_id==owner->class_id)return DOK_STATUS_OK;
    if(o->class_id==9u){if(s->services.hit)return s->services.hit(s->services.context,*target,p->owner,p->damage,accepted);*accepted=(s->items->collision->flags[glyph]&16u)==0u;}
    else if(o->class_id==1u){
        *accepted=1;return hurt_hero(s,p->owner,p->damage,p->template_glyph);
    }else if(o->class_id==7u)*accepted=(s->items->collision->flags[glyph]&8u)==0u;
    else *accepted=(s->items->collision->flags[glyph]&0x18u)==0u;
    return DOK_STATUS_OK;
}
static dok_status move_projectile(dok_combat *s,dok_projectile *p,int16_t distance)
{
    int32_t sn,cs;if(!s->services.trig(s->services.context,(uint16_t)p->heading,&sn,&cs))return DOK_STATUS_INVALID_STATE;
    p->x=wrap((int64_t)p->x+sar(wrap((int64_t)sn*distance),15));
    p->z=wrap((int64_t)p->z-sar(wrap((int64_t)cs*distance),15));return DOK_STATUS_OK;
}
static dok_status projectile_callback(dok_combat *s,uint32_t slot,uint32_t generation)
{
    dok_projectile *p=&s->projectiles[slot];dok_status r;dok_sprite_frame f;const dok_sprite_animation *a;uint32_t t,change_interval=0,interval=0;
    if(!p->phase||p->generation!=generation)return dok_events_cancel_current(s->scheduler);
    if(!seed(s,p->owner)){release_cell(s,p);p->phase=0;--s->active;return dok_events_cancel_current(s->scheduler);}
    if(p->phase>=2u){
        a=animation(s,p->template_glyph,p->effect_hit?0x77:0x7b);if(!a)return DOK_STATUS_INVALID_STATE;
        p->cursor=word((int32_t)p->cursor+1);
        if((uint32_t)p->cursor>=a->count){
            release_cell(s,p);p->phase=0;--s->active;++s->revision;
            emit(s,DOK_COMBAT_REMOVED,reference(s,slot),p->owner,(dok_object_ref){0},p->damage,p->template_glyph);
            return dok_events_cancel_current(s->scheduler);
        }
        p->phase=3;change_interval=1;
    }else{
        int32_t distance;int16_t quarter;uint32_t n,step,hit=0;uint16_t cl=0;int16_t h=0;dok_object_ref target={0};
        release_cell(s,p);if((r=now(s,&t))!=DOK_STATUS_OK)return r;
        if((r=dok_projectile_distance(p->speed,t-p->last_ticks,s->items->world->definition->settings[3],&distance,&quarter))!=DOK_STATUS_OK)return r;
        n=(uint32_t)(distance/quarter);if(distance>0||n>65535u)return DOK_STATUS_LIMIT_REACHED;
        for(step=0;step<=n;++step){
            r=move_projectile(s,p,step?quarter:word(distance%quarter));if(r!=DOK_STATUS_OK)return r;
            uint32_t index=cell(s,p->x,p->z);
            r=contact(s,p,index,&hit,&cl,&h,&target);if(r!=DOK_STATUS_OK)return r;
            if(hit){
                const dok_level_object *o=object(s,p->template_glyph);dok_sprite_frame current;
                a=animation(s,p->template_glyph,DOK_PRESENT_ANIMATION);if(!a||dok_sprite_animation_get(a,p->cursor,&current)!=DOK_STATUS_OK)return DOK_STATUS_INVALID_STATE;
                p->phase=2;p->effect_hit=(uint8_t)(cl==1u||cl==9u);p->cursor=1;
                change_interval=1;interval=(uint32_t)(int32_t)current.interval;
                r=move_projectile(s,p,word(-(int32_t)quarter));if(r!=DOK_STATUS_OK)return r;
                if(h>0)p->y=-((int32_t)h-sar(wrap((int64_t)current.height*o->width_parameter),1));
                emit(s,p->effect_hit?DOK_COMBAT_IMPACT:DOK_COMBAT_MISS,reference(s,slot),p->owner,target,p->damage,p->template_glyph);break;
            }
        }
        acquire_cell(s,p);a=animation(s,p->template_glyph,DOK_PRESENT_ANIMATION);if(!a)return DOK_STATUS_INVALID_STATE;
        p->cursor=word((int32_t)p->cursor+1);if((uint32_t)p->cursor>=a->count)p->cursor=0;
        p->damage=dok_projectile_decay(p->damage,p->damage_floor);
        /* Source clock_start is at the tail, after animation/decay. */
        if((r=now(s,&p->last_ticks))!=DOK_STATUS_OK)return r;
    }
    if(dok_sprite_animation_get(a,p->cursor,&f)!=DOK_STATUS_OK)return DOK_STATUS_INVALID_STATE;
    ++s->revision;
    /* Native normal flight never calls set_interval: it retains416 ticks.
     * Impact installs the OLD flight image interval before selecting flight
     * again; subsequent effect callbacks install their own frame intervals. */
    if(p->phase==3u)interval=(uint32_t)(int32_t)f.interval;
    return change_interval?dok_events_set_interval(s->scheduler,interval):DOK_STATUS_OK;
}
dok_status dok_combat_callback(dok_combat *s,dok_object_ref owner,uint16_t kind,uint32_t parameter)
{
    dok_status r=valid(s);uint32_t slot;if(r!=DOK_STATUS_OK)return r;
    if(owner.world_epoch!=s->epoch||owner.generation!=1u)return DOK_STATUS_STALE_REFERENCE;
    if(!s->scheduler->_in_callback||s->scheduler->_current>=DOK_EVENT_OWNER_LIMIT||
       !same(s->scheduler->_bank->owners[s->scheduler->_current].reference,owner))return DOK_STATUS_INVALID_STATE;
    s->busy=1;++s->callbacks;
    if(kind==DOK_COMBAT_PROJECTILE&&owner.object_id>=POOL&&owner.object_id<POOL+256u){
        slot=owner.object_id-POOL;s->executing=slot;r=projectile_callback(s,slot,parameter);s->executing=UINT32_MAX;
    }else if(kind==DOK_COMBAT_WEAPON&&parameter<DOK_WEAPON_SLOTS&&owner.object_id==WEAPON+parameter){
        uint32_t cancel;dok_sprite_frame f;const dok_world_object_seed *weapon=seed(s,s->items->inventory.weapons[parameter]);
        const dok_sprite_animation *a=weapon?animation(s,s->items->world->definition->objects[weapon->definition_index].glyph,DOK_PRESENT_ANIMATION):NULL;
        if(!a)r=DOK_STATUS_INVALID_STATE;
        else if((r=dok_weapon_step(&s->weapons[parameter],a->count,&cancel))==DOK_STATUS_OK){
            if(cancel){s->weapons[parameter].active=0;r=dok_events_cancel_current(s->scheduler);}
            else if(dok_sprite_animation_get(a,s->weapons[parameter].cursor,&f)!=DOK_STATUS_OK)r=DOK_STATUS_INVALID_STATE;
            else r=dok_events_set_interval(s->scheduler,(uint32_t)(int32_t)f.interval);
        }
    }else if(kind==DOK_COMBAT_SWITCH&&owner.object_id==WEAPON+DOK_WEAPON_SLOTS){
        dok_inventory *inv=&s->items->inventory;
        if(inv->selected>=DOK_WEAPON_SLOTS||inv->pending>=DOK_WEAPON_SLOTS)r=DOK_STATUS_INVALID_STATE;
        else if(s->weapons[inv->selected].state==4){
            inv->selected=inv->pending;s->swap_active=0;r=show(s,inv->selected);
            if(r==DOK_STATUS_OK)r=dok_events_cancel_current(s->scheduler);
        }
    }else r=DOK_STATUS_INVALID_ARGUMENT;
    s->busy=0;if(r!=DOK_STATUS_OK)s->fault=(uint32_t)r;return r;
}
static dok_status remove(dok_combat *s,uint32_t i)
{
    dok_projectile *p=&s->projectiles[i];const dok_event_owner *o;dok_status r;
    release_cell(s,p);p->phase=0;--s->active;++s->revision;
    /* Pool service owner lives until clear. Remove its event outside dispatch
     * by destroy+register, or let its generation check cancel at next sweep. */
    if(s->scheduler->_dispatching)return DOK_STATUS_OK;
    r=dok_events_inspect_owner(s->scheduler,service(s,POOL+i),&o);if(r!=DOK_STATUS_OK)return r;
    r=dok_events_destroy(s->scheduler,service(s,POOL+i));if(r!=DOK_STATUS_OK)return r;
    return dok_events_register(s->scheduler,service(s,POOL+i),2);
}
dok_status dok_combat_remove_owner(dok_combat *s,dok_object_ref owner)
{
    dok_status r=valid(s);if(r!=DOK_STATUS_OK)return r;
    for(uint32_t i=0;i<256u;++i)if(s->projectiles[i].phase&&same(s->projectiles[i].owner,owner))if((r=remove(s,i))!=DOK_STATUS_OK)return r;
    return DOK_STATUS_OK;
}
dok_status dok_combat_clear(dok_combat *s)
{
    dok_status r=valid(s);if(r!=DOK_STATUS_OK)return r;if(s->scheduler->_dispatching)return DOK_STATUS_INVALID_STATE;
    if((r=dok_collision_projectiles(s->items->collision,NULL))!=DOK_STATUS_OK)return r;
    if(s->registered){
        for(uint32_t i=0;i<256u;++i)if((r=dok_events_destroy(s->scheduler,service(s,POOL+i)))!=DOK_STATUS_OK)return r;
        for(uint32_t i=0;i<=DOK_WEAPON_SLOTS;++i)if((r=dok_events_destroy(s->scheduler,service(s,WEAPON+i)))!=DOK_STATUS_OK)return r;
    }
    memset(s->occupied,0,sizeof(s->occupied));for(uint32_t i=0;i<256u;++i)s->projectiles[i].phase=s->projectiles[i].occupied=0;
    s->registered=s->active=s->swap_active=0;s->tag=0;return DOK_STATUS_OK;
}
dok_status dok_combat_append_sprites(const dok_combat *s,const dok_nds_render_camera *camera,dok_sprite_list *list)
{
    uint32_t i;dok_status r=valid(s);if(r!=DOK_STATUS_OK)return r;
    if(!camera||!list||list->world_epoch!=s->epoch)return DOK_STATUS_INVALID_ARGUMENT;
    for(i=0;i<256u;++i){const dok_projectile *p=&s->projectiles[i];dok_sprite_pose pose={0};dok_sprite_projection projection;dok_sprite_frame f;
        const dok_level_object *o;const dok_sprite_animation *a;if(!p->phase)continue;
        o=object(s,p->template_glyph);a=animation(s,p->template_glyph,p->phase==3u?(p->effect_hit?0x77:0x7b):DOK_PRESENT_ANIMATION);
        if(!o||!a||dok_sprite_animation_get(a,p->cursor,&f)!=DOK_STATUS_OK)return DOK_STATUS_INVALID_STATE;
        pose.x=p->x;pose.y=p->y;pose.z=p->z;pose.width=word((int32_t)f.width*o->width_parameter);pose.height=word((int32_t)f.height*o->width_parameter);
        pose.flags=8;pose.glyph=p->template_glyph;pose.texture_width=f.width;pose.texture_height=f.height;pose.image=f.image;
        pose.source_id=UINT32_C(0x90000000)|i;
        if((r=dok_sprite_project(camera,&pose,&projection))!=DOK_STATUS_OK)return r;
        if(!projection.visible)continue;if(list->count==DOK_SPRITE_LIMIT)return DOK_STATUS_LIMIT_REACHED;
        list->poses[list->count++]=pose;
    }return DOK_STATUS_OK;
}
dok_status dok_combat_weapon_view(const dok_combat *s,dok_weapon_view *out)
{
    dok_weapon_view view={0};dok_status r=valid(s);uint32_t slot;const dok_world_object_seed *weapon;const dok_sprite_animation *a;
    if(r!=DOK_STATUS_OK)return r;if(!out)return DOK_STATUS_INVALID_ARGUMENT;
    slot=s->items->inventory.selected;if(slot>=DOK_WEAPON_SLOTS)return DOK_STATUS_INVALID_STATE;
    if(!s->items->inventory.weapons[slot].object_id||s->weapons[slot].state==4){*out=view;return DOK_STATUS_OK;}
    weapon=seed(s,s->items->inventory.weapons[slot]);if(!weapon)return DOK_STATUS_STALE_REFERENCE;
    a=animation(s,s->items->world->definition->objects[weapon->definition_index].glyph,DOK_PRESENT_ANIMATION);
    if(!a||dok_sprite_animation_get(a,s->weapons[slot].cursor,&view.frame)!=DOK_STATUS_OK)return DOK_STATUS_INVALID_STATE;
    /* PKR1068:2EC9, native viewport AA22=0/AA2A=319/AA2E=159,
     * k=8, surface scales1, as measured DOS reference. SAR rounds down. */
    view.left=sar(319-(int32_t)view.frame.width,1);
    view.top=159-sar((int32_t)view.frame.height-2,(uint16_t)s->weapons[slot].shift&31u);
    view.visible=1;*out=view;return DOK_STATUS_OK;
}
