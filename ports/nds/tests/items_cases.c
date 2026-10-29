#include "items_cases.h"
#include "../platform/assets/file_reader.h"
#include <string.h>
#define CHECK(e) do{++r->checks;if(!(e)){++r->failures;if(!r->line)r->line=__LINE__;return 1;}}while(0)
static void sink(void *context,const dok_item_output *o)
{ dok_items_cases_report *r=context;(void)o;++r->notifications; }
static int16_t value(const dok_level_object *o,uint16_t off)
{const dok_level_field *f=dok_level_field_at(o,off);return f?(int16_t)f->value:0;}
static dok_status ticks(void *context,uint32_t *out){(void)context;*out=0;return DOK_STATUS_OK;}
static void callback(void *context,dok_event_scheduler *scheduler,dok_object_ref owner,uint16_t kind,uint32_t parameter)
{(void)context;(void)scheduler;(void)owner;(void)kind;(void)parameter;}
int dok_items_cases(dok_items_cases_workspace *s,dok_items_cases_report *r,const char *path)
{
    NdsAssetFile file;dok_asset_reader reader;dok_asset_session session;uint32_t level,cell,i,a,changed;
    memset(r,0,sizeof(*r));
    CHECK(nds_asset_file_open(&file,path,&reader)==DOK_ASSET_OK);
    CHECK(dok_assets_open(&reader,&s->io,&session)==DOK_ASSET_OK);
    for(level=1;level<=3u;++level){
        dok_initial_world *w=&s->world.initial;dok_items *items=&s->items;dok_inventory initial;
        CHECK(dok_render_world_load(&session,level,level,&s->world,&s->scratch,s->assets,sizeof(s->assets))==DOK_STATUS_OK);
        CHECK(dok_collision_bind(&s->collision,w)==DOK_STATUS_OK);
        CHECK(dok_items_bind(items,w,&s->collision,sink,r)==DOK_STATUS_OK);initial=items->inventory;
        {dok_event_config config={NULL,ticks,NULL,callback,w->epoch};dok_interaction_services services={0};
         CHECK(dok_events_init(&s->scheduler,&s->events,&config)==DOK_STATUS_OK);
         CHECK(dok_interactions_bind(&s->interactions,w,&s->collision,&s->scheduler,&services)==DOK_STATUS_OK);
         CHECK(dok_interactions_register(&s->interactions)==DOK_STATUS_OK);}
        CHECK(initial.health==100&&initial.charge==0&&initial.max_health==100&&initial.max_charge==100);
        for(cell=0;cell<(uint32_t)s->world.effective.settings[0]*(uint16_t)s->world.effective.settings[2];++cell){
            unsigned char glyph=w->maps[DOK_WORLD_TMAP][cell];uint32_t index=w->glyph_index[glyph];
            const dok_level_object *o=&w->definition->objects[index-1u];dok_motion_state motion={0};
            if(o->class_id!=3u&&o->class_id!=10u)continue;
            ++r->placements;items->inventory=initial;motion.x=(int32_t)(cell%(uint16_t)w->definition->settings[0])*480+240;
            motion.z=(int32_t)(cell/(uint16_t)w->definition->settings[0])*480+240;
            if(value(o,0x54)==0&&value(o,0x56)>0){
                uint32_t notices=r->notifications;
                CHECK(dok_items_pickup(items,&motion,&a)==DOK_STATUS_OK&&!a);
                CHECK(w->maps[DOK_WORLD_TMAP][cell]==glyph&&r->notifications==notices);
            }
            items->inventory.health=50;items->inventory.charge=0;
            CHECK(dok_items_pickup(items,&motion,&a)==DOK_STATUS_OK&&a);
            CHECK(w->maps[DOK_WORLD_TMAP][cell]==' ');
            if(value(o,0x54)==10)CHECK(items->inventory.keys==(uint16_t)value(o,0x56));
            if(value(o,0x54)==10){
                for(i=0;i<w->object_count;++i)if(w->objects[i].class_id==7u&&
                    ((uint16_t)value(&w->definition->objects[i],0x5c)&items->inventory.keys)){
                    uint32_t bits=0x800,before=s->scheduler._event_count;
                    CHECK(dok_interactions_event(&s->interactions,w->objects[i].reference,w->hero,&bits,items->inventory.keys,8)==DOK_STATUS_OK);
                    CHECK(!bits&&s->scheduler._event_count>before);
                    CHECK(items->inventory.keys==(uint16_t)value(o,0x56));
                    CHECK(dok_events_clear(&s->scheduler)==DOK_STATUS_OK);
                }
            }
            if(value(o,0x54)==200)CHECK(items->inventory.map_available==1);
            if(o->class_id==10u){
                uint32_t slot=(uint16_t)value(o,0x81);dok_object_ref weapon=items->inventory.weapons[slot];
                CHECK(weapon.object_id==index&&items->inventory.max_charge==150&&items->inventory.charge==150);
                w->maps[DOK_WORLD_TMAP][cell]=glyph;
                CHECK(dok_items_pickup(items,&motion,&a)==DOK_STATUS_OK&&a);
                CHECK(items->inventory.max_charge==200&&items->inventory.weapons[slot].object_id==weapon.object_id);
            }
            CHECK(dok_items_pickup(items,&motion,&a)==DOK_STATUS_OK&&!a);
            /* Shared flags/definition and every other cell are untouched. */
            CHECK(s->collision.flags[glyph]==o->flags);
            for(i=cell+1u;i<(uint32_t)w->definition->settings[0]*(uint16_t)w->definition->settings[2];++i)
                CHECK(w->maps[DOK_WORLD_TMAP][i]==w->definition->maps[DOK_LEVEL_TMAP][
                    ((uint16_t)w->definition->settings[2]-1u-i/(uint16_t)w->definition->settings[0])*(uint16_t)w->definition->settings[0]+i%(uint16_t)w->definition->settings[0]] ||
                    (w->maps[DOK_WORLD_TMAP][i]==' '&&w->definition->maps[DOK_LEVEL_TMAP][
                    ((uint16_t)w->definition->settings[2]-1u-i/(uint16_t)w->definition->settings[0])*(uint16_t)w->definition->settings[0]+i%(uint16_t)w->definition->settings[0]]=='.'));
        }
        items->inventory=initial;
        CHECK(dok_items_next_weapon(items,&changed)==DOK_STATUS_OK&&!changed);
        items->inventory.weapons[1]=w->hero;items->inventory.selected=items->inventory.pending=1;
        CHECK(dok_items_next_weapon(items,&changed)==DOK_STATUS_OK&&!changed);
        items->inventory.weapons[2]=w->hero;
        CHECK(dok_items_next_weapon(items,&changed)==DOK_STATUS_OK&&changed&&items->inventory.pending==2);
        CHECK(dok_items_next_weapon(items,&changed)==DOK_STATUS_OK&&!changed&&items->inventory.pending==1);
        items->inventory.health=32760;items->inventory.max_health=32767;
        CHECK(dok_items_stat(items,0,20,&a)==DOK_STATUS_OK&&a&&items->inventory.health==0);
        CHECK(dok_items_stat(items,0,-10,&a)==DOK_STATUS_OK&&a&&items->inventory.health==0);
        CHECK(dok_items_stat(items,11,1,&a)==DOK_STATUS_OK&&!a);
        items->busy=1;CHECK(dok_items_stat(items,0,1,&a)==DOK_STATUS_INVALID_STATE);items->busy=0;
        ++w->epoch;CHECK(dok_items_pickup(items,&(dok_motion_state){0},&a)==DOK_STATUS_STALE_REFERENCE);
    }
    nds_asset_file_close(&file);return 0;
}
