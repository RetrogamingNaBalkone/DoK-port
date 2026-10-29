#include "dok/items.h"
#include <limits.h>
#include <stddef.h>
#include <string.h>
#define TAG UINT32_C(0x49544531)
static int16_t word(uint32_t n)
{ uint32_t v=(uint16_t)n;return v<=32767u?(int16_t)v:(int16_t)((int32_t)v-65536); }
static int16_t field(const dok_level_object *o,uint16_t offset)
{ const dok_level_field *f=dok_level_field_at(o,offset);return f?word((uint32_t)f->value):0; }
static dok_status enter(dok_items *s)
{
    if(!s||s->tag!=TAG||!s->world||!s->collision)return DOK_STATUS_INVALID_STATE;
    if(s->epoch!=s->world->epoch)return DOK_STATUS_STALE_REFERENCE;
    if(s->busy||s->collision->_active)return DOK_STATUS_INVALID_STATE;
    if(s->revision==UINT32_MAX)return DOK_STATUS_LIMIT_REACHED;
    s->busy=1;return DOK_STATUS_OK;
}
static void emit(dok_items *s,uint32_t kind,dok_object_ref object,uint32_t cell,
    int16_t category,int16_t value,uint16_t slot)
{
    dok_item_output o;memset(&o,0,sizeof(o));o.kind=kind;o.object=object;
    o.cell=cell;o.category=category;o.value=value;o.slot=slot;
    if(s->sink)s->sink(s->sink_context,&o);
}
static uint32_t stat(dok_items *s,int16_t c,int16_t v)
{
    dok_inventory *p=&s->inventory;int16_t *dst=NULL,n;
    if(c==0||c==1){
        int16_t maximum=c==0?p->max_health:p->max_charge;
        dst=c==0?&p->health:&p->charge;
        if(v>0&&*dst==maximum)return 0;
        n=word((uint32_t)*dst+(uint32_t)v);
        if(n<0)n=0;else if(n>maximum)n=maximum;
        *dst=n;
    }else if(c==2||c==3){
        dst=c==2?&p->max_health:&p->max_charge;
        *dst=word((uint32_t)*dst+(uint32_t)v);
        if(c==2)p->health=*dst;else p->charge=*dst;
    }else if(c==10){p->keys|=(uint16_t)v;n=word(p->keys);}
    else if(c==99){p->score=word((uint32_t)p->score+(uint32_t)v);n=p->score;}
    else if(c==200){p->map_available=1;n=1;}
    else if(c>=4&&c<=9){dst=&p->native_66_70[c-4];*dst=word((uint32_t)*dst+(uint32_t)v);}
    else if(c>=12&&c<=14){dst=&p->native_76_7a[c-12];*dst=word((uint32_t)*dst+(uint32_t)v);}
    else return 0;
    if(dst)n=*dst;
    ++s->revision;emit(s,DOK_ITEM_STAT,s->world->hero,UINT32_MAX,c,n,0);
    if(c==200)emit(s,DOK_ITEM_MAP,s->world->hero,UINT32_MAX,c,n,0);
    return 1;
}
dok_status dok_items_bind(dok_items *s,dok_initial_world *w,dok_collision_state *c,
    dok_item_sink_fn sink,void *context)
{
    uint32_t i;const dok_level_object *o=NULL;dok_collision_view view;
    if(!s||!w||!c)return DOK_STATUS_INVALID_ARGUMENT;
    if(c->_world!=w||c->_active||dok_collision_get_view(c,&view)!=DOK_STATUS_OK)return DOK_STATUS_INVALID_STATE;
    for(i=0;i<w->object_count;++i)if(w->objects[i].reference.object_id==w->hero.object_id){o=&w->definition->objects[i];break;}
    if(!o||o->class_id!=1u)return DOK_STATUS_INVALID_STATE;
    memset(s,0,sizeof(*s));s->world=w;s->collision=c;s->epoch=w->epoch;
    s->tag=TAG;s->revision=1;s->sink=sink;s->sink_context=context;
    s->inventory.health=field(o,0x5e);s->inventory.charge=field(o,0x60);
    s->inventory.max_health=field(o,0x62);s->inventory.max_charge=field(o,0x64);
    for(i=0;i<6u;++i)s->inventory.native_66_70[i]=field(o,(uint16_t)(0x66u+i*2u));
    for(i=0;i<3u;++i)s->inventory.native_76_7a[i]=field(o,(uint16_t)(0x76u+i*2u));
    s->inventory.keys=(uint16_t)field(o,0x72);
    return DOK_STATUS_OK;
}
dok_status dok_items_stat(dok_items *s,int16_t category,int16_t value,uint32_t *out)
{
    dok_status r;if(!out)return DOK_STATUS_INVALID_ARGUMENT;r=enter(s);if(r!=DOK_STATUS_OK)return r;
    *out=stat(s,category,value);s->busy=0;return DOK_STATUS_OK;
}
dok_status dok_items_pickup(dok_items *s,const dok_motion_state *m,uint32_t *out)
{
    dok_status r;dok_initial_world *w;const dok_level_definition *d;const dok_level_object *o;
    uint32_t cell,index,accepted=0;int32_t x,z;int16_t category,value,slot=0;
    if(!m||!out)return DOK_STATUS_INVALID_ARGUMENT;r=enter(s);if(r!=DOK_STATUS_OK)return r;
    w=s->world;d=w->definition;x=m->x/d->settings[3];z=m->z/d->settings[3];
    if(m->x<0)x=0;if(m->z<0)z=0;
    if(x>=d->settings[0])x=d->settings[0]-1;if(z>=d->settings[2])z=d->settings[2]-1;
    cell=(uint32_t)z*(uint16_t)d->settings[0]+(uint32_t)x;
    index=w->glyph_index[w->maps[DOK_WORLD_TMAP][cell]];
    if(!index||index>w->object_count){s->busy=0;return DOK_STATUS_INVALID_STATE;}
    o=&d->objects[index-1u];
    if(o->class_id==3u||o->class_id==10u){
        category=field(o,0x54);value=field(o,0x56);
        if(o->class_id==10u){
            slot=field(o,0x81);
            if(slot<0||slot>=(int16_t)DOK_WEAPON_SLOTS){s->busy=0;return DOK_STATUS_INVALID_STATE;}
        }
        accepted=stat(s,category,value);
        if(accepted||o->class_id==10u){
            emit(s,DOK_ITEM_PICKUP,w->objects[index-1u].reference,cell,category,value,(uint16_t)slot);
            if(category!=1000){w->maps[DOK_WORLD_TMAP][cell]=' ';++s->revision;accepted=1;}
            if(o->class_id==10u){
                dok_inventory *p=&s->inventory;
                if(!p->weapons[slot].object_id)p->weapons[slot]=w->objects[index-1u].reference;
                p->pending=(uint16_t)slot;
                if(p->selected>=DOK_WEAPON_SLOTS||!p->weapons[p->selected].object_id)p->selected=(uint16_t)slot;
                emit(s,DOK_ITEM_WEAPON,p->weapons[slot],cell,category,value,(uint16_t)slot);
            }
        }
    }
    *out=accepted;s->busy=0;return DOK_STATUS_OK;
}
dok_status dok_items_next_weapon(dok_items *s,uint32_t *out)
{
    uint32_t i,start;dok_status r;if(!out)return DOK_STATUS_INVALID_ARGUMENT;
    r=enter(s);if(r!=DOK_STATUS_OK)return r;
    start=s->inventory.pending<DOK_WEAPON_SLOTS?s->inventory.pending:0u;*out=0;
    for(i=1;i<DOK_WEAPON_SLOTS;++i){uint32_t n=(start+i)%DOK_WEAPON_SLOTS;
        if(s->inventory.weapons[n].object_id){s->inventory.pending=(uint16_t)n;
            *out=n!=s->inventory.selected;++s->revision;break;}}
    s->busy=0;return DOK_STATUS_OK;
}
