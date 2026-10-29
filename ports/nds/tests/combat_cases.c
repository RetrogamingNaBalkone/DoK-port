#include "combat_cases.h"
#include "../platform/assets/file_reader.h"
#include <string.h>
#define CHECK(e) do{++r->checks;if(!(e)){++r->failures;if(!r->line)r->line=__LINE__;return 1;}}while(0)
static dok_status read_ticks(void *p,uint32_t *out){*out=((dok_combat_cases_workspace *)p)->ticks;return DOK_STATUS_OK;}
static int trig(void *p,uint32_t h,int32_t *sn,int32_t *cs){(void)p;return dok_render_trig(h,sn,cs);}
static void output(void *p,const dok_combat_output *o)
{dok_combat_cases_workspace *s=p;if(o->kind<9u)++s->notices[o->kind];}
static dok_status hit(void *p,dok_object_ref target,dok_object_ref source,int16_t damage,uint32_t *accepted)
{
    dok_combat_cases_workspace *s=p;uint32_t i=target.object_id-1u;(void)source;
    if(i>=256u)return DOK_STATUS_STALE_REFERENCE;
    *accepted=s->target_health[i]>0;
    if(!*accepted)return DOK_STATUS_OK;
    if(!s->target_awake[i]){s->target_awake[i]=1;++s->wakes;}
    else {int16_t health=(int16_t)((uint16_t)s->target_health[i]-(uint16_t)damage);s->target_health[i]=health<0?0:health;++s->hits;}
    return DOK_STATUS_OK;
}
static void callback(void *p,dok_event_scheduler *scheduler,dok_object_ref owner,uint16_t kind,uint32_t parameter)
{dok_combat_cases_workspace *s=p;(void)scheduler;if(dok_combat_callback(&s->combat,owner,kind,parameter)!=DOK_STATUS_OK)s->fault=1;}
static dok_status bind(dok_combat_cases_workspace *s)
{
    dok_event_config config={s,read_ticks,s,callback,s->world.initial.epoch};dok_combat_services services={s,output,hit,trig};
    dok_status e;memset(s->notices,0,sizeof(s->notices));s->ticks=s->fault=s->hits=s->wakes=0;
    memset(s->target_awake,0,sizeof(s->target_awake));
    for(uint32_t i=0;i<256u;++i)s->target_health[i]=42;
    if((e=dok_collision_bind(&s->collision,&s->world.initial))!=DOK_STATUS_OK)return e;
    if((e=dok_items_bind(&s->items,&s->world.initial,&s->collision,NULL,NULL))!=DOK_STATUS_OK)return e;
    if((e=dok_events_init(&s->scheduler,&s->events,&config))!=DOK_STATUS_OK)return e;
    if((e=dok_combat_bind(&s->combat,&s->items,&s->scheduler,&s->world.presentation,&s->bank,&services))!=DOK_STATUS_OK)return e;
    return dok_combat_register(&s->combat);
}
static dok_status sweep(dok_combat_cases_workspace *s,uint32_t ticks)
{dok_event_dispatch_report report;s->ticks=ticks;return dok_events_dispatch(&s->scheduler,&report);}
int dok_combat_cases(dok_combat_cases_workspace *s,dok_combat_cases_report *r,const char *path)
{
    NdsAssetFile file;dok_asset_reader reader;dok_asset_session session;uint32_t level,glyph,i,t,language,language_hashes[3]={0};
    memset(r,0,sizeof(*r));r->trace_hash=2166136261u;
    CHECK(nds_asset_file_open(&file,path,&reader)==DOK_ASSET_OK);CHECK(dok_assets_open(&reader,&s->io,&session)==DOK_ASSET_OK);
    for(language=1;language<=2u;++language)for(level=1;level<=3u;++level){
        CHECK(dok_render_world_load(&session,level,level,&s->world,&s->scratch,s->assets,sizeof(s->assets))==DOK_STATUS_OK);
        uint32_t off=(s->world.asset_bytes+31u)&~31u;
        if(language==2u){dok_asset_view view;dok_asset_id id=(UINT32_C(2)<<30)|((17u+level)<<18)|((uint32_t)DOK_ASSET_LEVEL_CP866<<10);
            CHECK(dok_assets_load(&session,id,s->assets+off,sizeof(s->assets)-off,&view)==DOK_ASSET_OK);
            CHECK(dok_level_parse(&view,&s->scratch.world.level,&s->world.base)==DOK_WORLD_OK);
            CHECK(dok_presentation_bind(&s->world.effective,&s->world.base,&session,&s->scratch.presentation,&s->world.presentation)==DOK_BALANCE_OK);
            off=(off+view.entry.bytes+31u)&~31u;}
        CHECK(dok_sprite_assets_load(&s->world.presentation,&s->bank,s->assets+off,sizeof(s->assets)-off)==DOK_STATUS_OK);
        CHECK(bind(s)==DOK_STATUS_OK);
        dok_control_commands input={0};input.native_commands=DOK_INPUT_NATIVE_FIRE;
        CHECK(dok_combat_controls(&s->combat,&input)==DOK_STATUS_OK&&s->combat.active==0);
        for(i=0;i<s->world.initial.object_count;++i)if(s->world.effective.objects[i].class_id==10u){
            uint32_t slot=(uint16_t)dok_level_field_at(&s->world.effective.objects[i],0x81)->value;
            s->items.inventory.weapons[slot]=s->world.initial.objects[i].reference;
        }
        s->items.inventory.selected=s->items.inventory.pending=1;
        CHECK(dok_combat_controls(&s->combat,&input)==DOK_STATUS_OK&&s->combat.weapons[1].state==1);
        for(t=50;t<=4000;t+=50)CHECK(sweep(s,t)==DOK_STATUS_OK&&!s->fault);
        CHECK(s->combat.weapons[1].state==7);
        {dok_weapon_view view;CHECK(dok_combat_weapon_view(&s->combat,&view)==DOK_STATUS_OK&&view.visible);
         CHECK(view.left==(319-(int32_t)view.frame.width)/2&&view.top==161-(int32_t)view.frame.height);
         unsigned char *overlay=s->assets+sizeof(s->assets)-49152u;
         const dok_asset_entry *image=dok_sprite_asset_image(&s->bank,view.frame.image);CHECK(image!=NULL);
         dok_asset_view raw;CHECK(dok_assets_load(&session,image->id,s->assets+sizeof(s->assets)-131072u,65536u,&raw)==DOK_ASSET_OK);
         memset(overlay,0xa5,49152u);CHECK(dok_weapon_overlay(&view,raw.data,image->storage_width,image->bytes,overlay,49152u)==DOK_STATUS_OK);
         for(uint32_t y=0;y<192u;++y)for(uint32_t x=0;x<256u;++x){
             if(y>=154u)CHECK(overlay[y*256u+x]==0xa5u);
             else {int32_t xx=(int32_t)(x*5u/4u)-view.left,yy=(int32_t)(y*25u/24u)-view.top;
                 unsigned char expected=xx>=0&&xx<view.frame.width&&yy>=0&&yy<view.frame.height?raw.data[(uint32_t)yy*image->storage_width+(uint32_t)xx]:0;
                 CHECK(overlay[y*256u+x]==expected);}
         }}
        s->items.inventory.charge=1;
        CHECK(dok_combat_controls(&s->combat,&input)==DOK_STATUS_OK&&s->combat.active==1&&s->items.inventory.charge==0);
        CHECK(dok_combat_controls(&s->combat,&input)==DOK_STATUS_OK&&s->combat.active==1);
        for(t=4050;t<=20000;t+=50)CHECK(sweep(s,t)==DOK_STATUS_OK&&!s->fault);
        CHECK(!s->combat.active);
        input.native_commands=0;input.next_weapon_pressed=1;
        CHECK(dok_combat_controls(&s->combat,&input)==DOK_STATUS_OK&&s->items.inventory.pending==2);
        for(t=20050;t<=30000;t+=50)CHECK(sweep(s,t)==DOK_STATUS_OK&&!s->fault);
        CHECK(s->items.inventory.selected==2&&s->combat.weapons[2].state==7&&!s->combat.swap_active);
        CHECK(dok_combat_clear(&s->combat)==DOK_STATUS_OK&&!s->collision._projectile_occupied&&s->scheduler._event_count==0);
        /* Fill every available native slot in source order. Refusal preserves
         * caller output; remove-owner releases all instances and events. */
        CHECK(bind(s)==DOK_STATUS_OK);
        {dok_object_ref instance={0},previous={0};uint32_t count=0;
         const dok_world_object_seed *hero=&s->world.initial.objects[s->world.initial.hero.object_id-1u];
         while(dok_combat_spawn(&s->combat,hero->reference,'1',hero->x,hero->z,hero->heading,&instance)==DOK_STATUS_OK){
             CHECK(instance.object_id>previous.object_id);previous=instance;CHECK(++count<=256u);}
         CHECK(count>0&&count==s->combat.active&&instance.object_id==previous.object_id);
         CHECK(dok_combat_remove_owner(&s->combat,hero->reference)==DOK_STATUS_OK&&s->combat.active==0&&s->scheduler._event_count==0);
         CHECK(dok_combat_spawn(&s->combat,hero->reference,'1',hero->x,hero->z,hero->heading,&instance)==DOK_STATUS_OK&&instance.generation==2);
         CHECK(dok_combat_clear(&s->combat)==DOK_STATUS_OK);}
        ++r->scenarios;
        for(glyph='1';glyph<='4';++glyph){
            uint32_t index=s->world.initial.glyph_index[glyph];dok_object_ref instance;
            if(!index)continue;CHECK(bind(s)==DOK_STATUS_OK);++r->scenarios;
            const dok_world_object_seed *hero=&s->world.initial.objects[s->world.initial.hero.object_id-1u];
            CHECK(dok_combat_spawn(&s->combat,hero->reference,(uint8_t)glyph,hero->x,hero->z,hero->heading,&instance)==DOK_STATUS_OK);
            uint32_t slot=instance.object_id-257u;dok_projectile *p=&s->combat.projectiles[slot];
            CHECK(p->y==-s->world.effective.objects[index-1u].height_parameter&&p->damage>0);
            CHECK(sweep(s,415)==DOK_STATUS_OK&&p->cursor==0&&p->last_ticks==0);
            CHECK(sweep(s,416)==DOK_STATUS_OK&&!s->fault&&s->combat.callbacks==1);
            if(p->phase==1u){const dok_event_owner *owner;
                CHECK(dok_events_inspect_owner(&s->scheduler,(dok_object_ref){s->world.initial.epoch,0x10000u+slot,1},&owner)==DOK_STATUS_OK);
                CHECK(owner->count==1&&owner->events[0].interval==416&&owner->events[0].deadline==832);}
            int16_t old=p->damage;CHECK(old==dok_projectile_decay((int16_t)dok_level_field_at(&s->world.effective.objects[index-1u],0x5c)->value,p->damage_floor));
            CHECK(dok_combat_remove_owner(&s->combat,hero->reference)==DOK_STATUS_OK&&!s->combat.active);
            CHECK(dok_combat_clear(&s->combat)==DOK_STATUS_OK);
        }
        /* A controlled target fixture isolates first wake vs subsequent damage. */
        for(i=0;i<s->world.initial.object_count;++i)if(s->world.initial.objects[i].class_id==9u&&s->world.initial.objects[i].pose_known){
            dok_object_ref instance;const dok_world_object_seed *target=&s->world.initial.objects[i];
            CHECK(bind(s)==DOK_STATUS_OK);++r->scenarios;
            CHECK(dok_combat_spawn(&s->combat,s->world.initial.hero,'1',target->x,target->z,0,&instance)==DOK_STATUS_OK);
            CHECK(sweep(s,416)==DOK_STATUS_OK&&!s->fault&&s->wakes==1&&s->target_health[i]==42);
            dok_projectile *p=&s->combat.projectiles[instance.object_id-257u];CHECK(p->phase==2&&p->effect_hit==1);
            CHECK(dok_combat_spawn(&s->combat,s->world.initial.hero,'1',target->x,target->z,0,&instance)==DOK_STATUS_OK);
            CHECK(sweep(s,832)==DOK_STATUS_OK&&!s->fault&&s->hits==1&&s->target_health[i]==28);
            CHECK(dok_combat_clear(&s->combat)==DOK_STATUS_OK);break;
        }
        /* Renderer is a read-only observer. Repeat identical event/input time
         * with30/60 observation cadence and compare complete instance bytes. */
        uint32_t hashes[2]={2166136261u,2166136261u};
        for(uint32_t cadence=0;cadence<2u;++cadence){
            dok_object_ref instance;CHECK(bind(s)==DOK_STATUS_OK);
            const dok_world_object_seed *hero=&s->world.initial.objects[s->world.initial.hero.object_id-1u];
            CHECK(dok_combat_spawn(&s->combat,hero->reference,'2',hero->x,hero->z,hero->heading,&instance)==DOK_STATUS_OK);
            for(t=0;t<=20000;t+=20){
                CHECK(sweep(s,t)==DOK_STATUS_OK&&!s->fault);
                if(t%(cadence?160u:320u)==0u){
                    dok_nds_render_camera camera={hero->x,hero->y,hero->z,0,32768};
                    s->list.count=0;s->list.world_epoch=s->world.initial.epoch;s->list.revision=1;
                    CHECK(dok_combat_append_sprites(&s->combat,&camera,&s->list)==DOK_STATUS_OK);
                }
                const unsigned char *bytes=(const unsigned char *)s->combat.projectiles;
                for(uint32_t b=0;b<sizeof(s->combat.projectiles);++b)hashes[cadence]=(hashes[cadence]^bytes[b])*16777619u;
            }
            r->callbacks+=s->combat.callbacks;CHECK(dok_combat_clear(&s->combat)==DOK_STATUS_OK);
        }
        CHECK(hashes[0]==hashes[1]);
        if(language==1u)language_hashes[level-1u]=hashes[0];else CHECK(language_hashes[level-1u]==hashes[0]);
        r->trace_hash=(r->trace_hash^hashes[0])*16777619u;
    }
    nds_asset_file_close(&file);return 0;
}
