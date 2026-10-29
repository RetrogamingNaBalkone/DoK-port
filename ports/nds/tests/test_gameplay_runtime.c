#include "../platform/gameplay_probe/gameplay.h"
#include "../platform/gameplay_probe/effects_runtime.h"
#include "ai_combat_cases.h"
#include "dok/water_texture.h"
#include "../platform/assets/file_reader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static struct {
    dok_render_world world;dok_render_workspace scratch;dok_asset_workspace io;
    dok_sprite_asset_bank bank;dok_sprite_list list;dok_event_bank events;
    NdsEffectsRuntime effects;NdsGameplay game;NdsEffectTrace traces[512];
    unsigned char assets[DOK_NDS_ASSETS_BYTES],pool[131072];
} w;
static uint32_t checks,language,level,rows,cadence;
static FILE *transcript;
static dok_event_callback_fn base_callback;
#define CHECK(e) do{++checks;if(!(e)){fprintf(stderr,"gameplay_runtime:%d\n",__LINE__);return 1;}}while(0)
static uint64_t counter(void *p){(void)p;return 0;}
static uint32_t hash(uint32_t h,const void *p,size_t n){const unsigned char *b=p;for(size_t i=0;i<n;++i)h=(h^b[i])*16777619u;return h;}
static uint32_t cell(int32_t x,int32_t z)
{int32_t a=x/480,b=z/480;if(a<0)a=0;if(a>63)a=63;if(b<0)b=0;if(b>63)b=63;return (uint32_t)b*64u+(uint32_t)a;}
static int32_t sar(int64_t n){return n>=0?(int32_t)(n/32768):-(int32_t)((-n+32767)/32768);}
static uint32_t class_at(uint32_t plane,int32_t x,int32_t z)
{
    uint32_t at=cell(x,z),g=w.world.initial.maps[plane][at];
    if(plane==DOK_WORLD_MMAP && (w.game.combat.occupied[at/8]&(1u<<(at%8))))
        for(uint32_t i=0;i<256u;++i)if(w.game.combat.projectiles[i].occupied&&w.game.combat.projectiles[i].cell==at){g=w.game.combat.projectiles[i].template_glyph;break;}
    return w.world.effective.objects[w.world.initial.glyph_index[g]-1u].class_id;
}
static void audit_callback(void *p,dok_event_scheduler *s,dok_object_ref owner,uint16_t kind,uint32_t parameter)
{
    if(kind!=DOK_AI_CALLBACK){base_callback(p,s,owner,kind,parameter);return;}
    dok_enemy *e=&w.game.ai.enemies[w.game.ai.glyph_index[parameter]-1u];dok_ai_combat_trace t={0};
    t.language=(int32_t)language;t.level=(int32_t)level;t.glyph=e->glyph;t.cadence=(int32_t)cadence;
    t.scenario=class_at(DOK_WORLD_TMAP,e->motion.x,e->motion.z)!=0u;
    t.state_in=e->state;t.health_in=e->health;t.next_in=e->next_cursor;t.kind_in=e->displayed_kind;t.cursor_in=e->displayed_cursor;
    t.course_in=e->course_counter;t.shots_in=e->shots;t.elapsed=(int32_t)(w.effects.fixture_ticks-e->burst_started);
    int32_t sn,cs;dok_render_trig((uint16_t)e->motion.heading,&sn,&cs);
    int32_t x=e->motion.x+sar((int64_t)sn*-480),z=e->motion.z-sar((int64_t)cs*-480);
    t.front_class=(int32_t)class_at(DOK_WORLD_MMAP,x,z);
    if(!t.front_class)t.front_class=(int32_t)class_at(DOK_WORLD_TMAP,x+sar((int64_t)sn*-480),z-sar((int64_t)cs*-480));
    int16_t heading=e->motion.heading;dok_world_object_seed *hero=&w.world.initial.objects[w.world.initial.hero.object_id-1u];
    uint32_t visible=0;
    if(e->health>0 && dok_ai_heading(hero->x-e->motion.x,hero->z-e->motion.z,&e->motion.heading)==DOK_STATUS_OK)
        if(dok_ai_sight(&w.game.ai,w.world.initial.objects[w.world.initial.glyph_index[e->glyph]-1u].reference,&visible)!=DOK_STATUS_OK)w.game.fault=99;
    e->motion.heading=heading;t.visible=(int32_t)visible;
    t.flags_in=w.game.collision.flags[e->glyph];t.height=w.world.effective.objects[w.world.initial.glyph_index[e->glyph]-1u].height_parameter;t.hero_in=w.game.items.inventory.health;
    uint32_t melee=w.game.ai_outputs[DOK_AI_MELEE],ranged=w.game.ai_outputs[DOK_AI_RANGED],dead=w.game.ai_outputs[DOK_AI_DEAD];
    base_callback(p,s,owner,kind,parameter);
    t.state_out=e->state;t.health_out=e->health;t.next_out=e->next_cursor;t.kind_out=e->displayed_kind;t.cursor_out=e->displayed_cursor;
    t.course_out=e->course_counter;t.shots_out=e->shots;t.flags_out=w.game.collision.flags[e->glyph];t.corpse_out=e->corpse;t.hero_out=w.game.items.inventory.health;
    t.melee=(int32_t)(w.game.ai_outputs[DOK_AI_MELEE]-melee);t.ranged=(int32_t)(w.game.ai_outputs[DOK_AI_RANGED]-ranged);t.dead=(int32_t)(w.game.ai_outputs[DOK_AI_DEAD]-dead);
    const dok_event_owner *event;if(dok_events_inspect_owner(s,owner,&event)==DOK_STATUS_OK&&event->count)t.interval=(int32_t)event->events[0].interval;
    t.plane=e->corpse?(w.world.initial.maps[DOK_WORLD_TMAP][cell(e->motion.x,e->motion.z)]==e->glyph?1:0):-1;
    int32_t fields[33];memcpy(fields,&t,sizeof(fields));fputc('[',transcript);for(uint32_t i=0;i<33u;++i)fprintf(transcript,"%s%ld",i?",":"",(long)fields[i]);fputs("]\n",transcript);++rows;
}
int main(int argc,char **argv)
{
    NdsAssetFile file;dok_asset_reader reader;dok_asset_session session;uint32_t expected[3]={0};
    CHECK(argc==3);transcript=fopen(argv[2],"wb");CHECK(transcript!=NULL);
    CHECK(nds_asset_file_open(&file,argv[1],&reader)==DOK_ASSET_OK);CHECK(dok_assets_open(&reader,&w.io,&session)==DOK_ASSET_OK);
    for(language=1;language<=2u;++language)for(level=1;level<=3u;++level)for(cadence=0;cadence<2u;++cadence){
        CHECK(dok_render_world_load(&session,level,level,&w.world,&w.scratch,w.assets,sizeof(w.assets))==DOK_STATUS_OK);
        uint32_t off=(w.world.asset_bytes+31u)&~31u;
        if(language==2){dok_asset_view view;dok_asset_id id=(UINT32_C(2)<<30)|((17u+level)<<18)|((uint32_t)DOK_ASSET_LEVEL_CP866<<10);
            CHECK(dok_assets_load(&session,id,w.assets+off,sizeof(w.assets)-off,&view)==DOK_ASSET_OK);
            CHECK(dok_level_parse(&view,&w.scratch.world.level,&w.world.base)==DOK_WORLD_OK);
            CHECK(dok_presentation_bind(&w.world.effective,&w.world.base,&session,&w.scratch.presentation,&w.world.presentation)==DOK_BALANCE_OK);off=(off+view.entry.bytes+31u)&~31u;}
        CHECK(dok_sprite_assets_load(&w.world.presentation,&w.bank,w.assets+off,sizeof(w.assets)-off)==DOK_STATUS_OK);
        dok_platform_services platform={NULL,counter,33513982,1,NULL};w.effects.traces=w.traces;w.effects.trace_total=0;
        CHECK(nds_effects_load(&w.effects,&w.world,&w.bank,&session,&reader,&w.events,&platform,w.pool,sizeof(w.pool))==DOK_STATUS_OK);
        w.effects.simulation_list=&w.list;CHECK(nds_gameplay_bind(&w.game,&w.effects)==DOK_STATUS_OK);
        base_callback=w.effects.scheduler._config.callback;w.effects.scheduler._config.callback=audit_callback;
        /* Explicit inventory fixture through original pickups, same as GPU combat routes. */
        for(uint32_t at=0;at<4096u;++at){uint32_t g=w.world.initial.maps[DOK_WORLD_TMAP][at],n=w.world.initial.glyph_index[g];
            if(w.world.effective.objects[n-1u].class_id==10u){dok_motion_state m=w.game.hero;uint32_t accepted;m.x=(int32_t)(at%64)*480+240;m.z=(int32_t)(at/64)*480+240;CHECK(dok_items_pickup(&w.game.items,&m,&accepted)==DOK_STATUS_OK);}}
        uint32_t h=2166136261u,next_observation=0;
        for(uint32_t ticks=0;ticks<=120000u;ticks+=20u){
            w.effects.fixture_ticks=ticks;
            if(ticks%100u==0u){uint32_t phase=ticks/10000u;uint32_t held=DOK_BUTTON_R|(phase<4u?DOK_BUTTON_UP:phase<8u?DOK_BUTTON_RIGHT:DOK_BUTTON_Y);
                if(ticks%2400u==0u)held|=DOK_BUTTON_SELECT;CHECK(nds_gameplay_input(&w.game,held)==DOK_STATUS_OK);}
            if(ticks==2000u||ticks==30000u){for(uint32_t i=0;i<w.game.ai.count;++i){dok_enemy *e=&w.game.ai.enemies[i];uint32_t n=w.world.initial.glyph_index[e->glyph];if(w.world.initial.objects[n-1u].pose_known){uint32_t accepted;
                CHECK(dok_ai_hit(&w.game.ai,w.world.initial.objects[n-1u].reference,w.world.initial.hero,ticks==2000u?1:32767,&accepted)==DOK_STATUS_OK);break;}}}
            dok_event_dispatch_report dispatch;CHECK(dok_events_dispatch(&w.effects.scheduler,&dispatch)==DOK_STATUS_OK);
            CHECK(!w.game.fault&&!w.game.ai.fault&&!w.game.combat.fault&&!w.effects.fault);
            if(ticks>=next_observation){dok_nds_render_camera camera={w.game.hero.x,w.game.hero.y,w.game.hero.z,0,0};const dok_sprite_list *list;
                CHECK(dok_render_trig((uint16_t)w.game.hero.heading,&camera.sin_q15,&camera.cos_q15));
                dok_sprite_snapshot snapshot={w.world.initial.epoch,1,w.bank.bindings,&w.world.initial};
                CHECK(dok_sprites_gather_live(&snapshot,w.game.collision.flags,w.game.ai.corpses,&camera,(uint16_t)w.game.hero.heading,&w.list,&list)==DOK_STATUS_OK);
                CHECK(dok_combat_append_sprites(&w.game.combat,&camera,&w.list)==DOK_STATUS_OK);next_observation+=cadence?166u:333u;}
            if(ticks%400u==0){
                h=hash(h,w.world.initial.maps,sizeof(w.world.initial.maps));h=hash(h,w.game.ai.enemies,w.game.ai.count*sizeof(dok_enemy));
                h=hash(h,w.game.combat.projectiles,sizeof(w.game.combat.projectiles));h=hash(h,&w.game.hero,sizeof(w.game.hero));
                h=hash(h,&w.game.items.inventory,sizeof(w.game.items.inventory));h=hash(h,w.game.collision.flags,sizeof(w.game.collision.flags));
                h=hash(h,w.game.interactions.doors,sizeof(w.game.interactions.doors));h=hash(h,&w.game.c2,sizeof(w.game.c2));
                h=hash(h,w.effects.water,sizeof(w.effects.water));h=hash(h,w.effects.material_phase,sizeof(w.effects.material_phase));
                h=hash(h,&w.events,sizeof(w.events));
                for(uint32_t g=0;g<256u;++g){dok_sprite_player *p=&w.effects.players[g];
                    h=hash(h,&p->cursor,sizeof(p->cursor));h=hash(h,&p->remaining,sizeof(p->remaining));
                    h=hash(h,&p->displayed.interval,sizeof(p->displayed.interval));}
            }
        }
        if(language==1&&cadence==0)expected[level-1u]=h;else CHECK(expected[level-1u]==h);
        CHECK(w.game.hero_callbacks>0&&w.game.ai.callbacks>0&&w.game.combat.callbacks>0);
        CHECK(dok_combat_clear(&w.game.combat)==DOK_STATUS_OK);CHECK(dok_ai_clear(&w.game.ai)==DOK_STATUS_OK);
    }
    fclose(transcript);nds_asset_file_close(&file);
    printf("{\"checks\":%lu,\"failures\":0,\"callbacks\":%lu,\"world_hashes\":[%lu,%lu,%lu]}\n",(unsigned long)checks,(unsigned long)rows,(unsigned long)expected[0],(unsigned long)expected[1],(unsigned long)expected[2]);return 0;
}
