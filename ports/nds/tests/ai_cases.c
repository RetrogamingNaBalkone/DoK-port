#include "ai_cases.h"
#include "../platform/assets/file_reader.h"
#include <string.h>
#define CHECK(e) do{++r->checks;if(!(e)){++r->failures;if(!r->line)r->line=__LINE__;return 1;}}while(0)
static dok_status time_now(void *p,uint32_t *out){*out=((dok_ai_cases_workspace *)p)->game.ticks;return DOK_STATUS_OK;}
static int trig(void *p,uint32_t h,int32_t *sn,int32_t *cs){(void)p;return dok_render_trig(h,sn,cs);}
static dok_status hit(void *p,dok_object_ref target,dok_object_ref source,int16_t damage,uint32_t *accepted)
{return dok_ai_hit(&((dok_ai_cases_workspace *)p)->ai,target,source,damage,accepted);}
static void callback(void *p,dok_event_scheduler *scheduler,dok_object_ref owner,uint16_t kind,uint32_t parameter)
{
    dok_ai_cases_workspace *s=p;uint32_t begin=s->profile_counter?s->profile_counter():0;dok_status status;(void)scheduler;
    if(kind==DOK_AI_CALLBACK)status=dok_ai_callback(&s->ai,owner,kind);
    else status=dok_combat_callback(&s->game.combat,owner,kind,parameter);
    if(status!=DOK_STATUS_OK)s->game.fault=(uint32_t)status;
    if(s->profile_counter&&kind==DOK_AI_CALLBACK){uint32_t elapsed=s->profile_counter()-begin;
        if(s->profile_stress){s->stress_ticks+=elapsed;if(elapsed>s->max_stress_ticks)s->max_stress_ticks=elapsed;++s->stress_samples;}
        else{s->callback_ticks+=elapsed;if(elapsed>s->max_callback_ticks)s->max_callback_ticks=elapsed;++s->profile_samples;}}
}
static dok_status bind(dok_ai_cases_workspace *s)
{
    dok_combat_cases_workspace *g=&s->game;dok_status status;g->world.initial=s->initial;g->ticks=g->fault=0;
    dok_event_config config={s,time_now,s,callback,g->world.initial.epoch};dok_combat_services combat={s,NULL,hit,trig};dok_ai_services ai={s,trig,NULL};
    if((status=dok_collision_bind(&g->collision,&g->world.initial))!=DOK_STATUS_OK)return status;
    if((status=dok_items_bind(&g->items,&g->world.initial,&g->collision,NULL,NULL))!=DOK_STATUS_OK)return status;
    if((status=dok_events_init(&g->scheduler,&g->events,&config))!=DOK_STATUS_OK)return status;
    if((status=dok_combat_bind(&g->combat,&g->items,&g->scheduler,&g->world.presentation,&g->bank,&combat))!=DOK_STATUS_OK)return status;
    if((status=dok_combat_register(&g->combat))!=DOK_STATUS_OK)return status;
    if((status=dok_ai_bind(&s->ai,&g->world.initial,&g->collision,&g->scheduler,&g->world.presentation,&g->bank,&g->combat,&ai))!=DOK_STATUS_OK)return status;
    if((status=dok_ai_register(&s->ai))!=DOK_STATUS_OK)return status;
    for(uint32_t i=0;i<s->ai.count;++i){dok_enemy *e=&s->ai.enemies[i];dok_presentation_value v;
        if(dok_presentation_get(&g->world.presentation,e->glyph,DOK_PRESENT_ANIMATION,&v)!=DOK_BALANCE_OK||dok_sprite_asset_select(&g->bank,e->glyph,v.asset_id,0)!=DOK_STATUS_OK)return DOK_STATUS_INVALID_STATE;}
    dok_world_object_seed *hero=&g->world.initial.objects[g->world.initial.hero.object_id-1u];
    return dok_motion_init(&s->hero,hero->x,hero->y,hero->z,hero->heading,5,120);
}
static dok_status sweep(dok_ai_cases_workspace *s,uint32_t t)
{dok_event_dispatch_report report;s->game.ticks=t;return dok_events_dispatch(&s->game.scheduler,&report);}
static dok_status hero_service(dok_ai_cases_workspace *s,uint32_t ordinal)
{
    dok_combat_cases_workspace *g=&s->game;dok_motion_state *m=&s->hero;dok_collision_context context;dok_collision_report collision;dok_motion_report motion;
    dok_nds_render_camera camera={m->x,m->y,m->z,0,0};dok_status r;uint32_t accepted;
    if(!dok_render_trig((uint16_t)m->heading,&camera.sin_q15,&camera.cos_q15))return DOK_STATUS_INVALID_STATE;
    if((r=dok_ai_consider_camera(&s->ai,&camera,(uint16_t)m->heading))!=DOK_STATUS_OK)return r;
    if((r=dok_items_pickup(&g->items,m,&accepted))!=DOK_STATUS_OK)return r;
    uint32_t phase=ordinal%120u;
    m->flags=phase<70u?DOK_INPUT_NATIVE_FORWARD:phase<100u?DOK_INPUT_NATIVE_TURN_NEG:DOK_INPUT_NATIVE_STRAFE_LEFT;
    if((r=dok_collision_begin(&g->collision,g->world.initial.hero,m,ordinal?416u:0u,&context))!=DOK_STATUS_OK)return r;
    dok_motion_services services={s,trig,&context,dok_collision_move};
    r=dok_motion_update(m,ordinal?416u:0u,&services,&motion);dok_status end=dok_collision_end(&context,&collision);
    if(r!=DOK_STATUS_OK)return r;if(end!=DOK_STATUS_OK)return end;
    dok_world_object_seed *hero=&g->world.initial.objects[g->world.initial.hero.object_id-1u];
    hero->x=m->x;hero->y=m->y;hero->z=m->z;hero->heading=m->heading;return DOK_STATUS_OK;
}
static uint32_t hash(uint32_t h,const void *data,uint32_t n)
{const unsigned char *b=data;for(uint32_t i=0;i<n;++i)h=(h^b[i])*16777619u;return h;}
int dok_ai_cases(dok_ai_cases_workspace *s,dok_ai_cases_report *r,const char *path)
{
    dok_combat_cases_workspace *g=&s->game;NdsAssetFile file;dok_asset_reader reader;dok_asset_session session;uint32_t level,language,language_hash[3]={0};
    memset(r,0,sizeof(*r));r->trace_hash=2166136261u;s->callback_ticks=s->max_callback_ticks=s->profile_samples=0;
    s->stress_ticks=s->max_stress_ticks=s->stress_samples=s->profile_stress=0;
    CHECK(nds_asset_file_open(&file,path,&reader)==DOK_ASSET_OK);CHECK(dok_assets_open(&reader,&g->io,&session)==DOK_ASSET_OK);
    for(language=1;language<=2u;++language)for(level=1;level<=3u;++level){
        CHECK(dok_render_world_load(&session,level,level,&g->world,&g->scratch,g->assets,sizeof(g->assets))==DOK_STATUS_OK);
        uint32_t off=(g->world.asset_bytes+31u)&~31u;
        if(language==2u){dok_asset_view view;dok_asset_id id=(UINT32_C(2)<<30)|((17u+level)<<18)|((uint32_t)DOK_ASSET_LEVEL_CP866<<10);
            CHECK(dok_assets_load(&session,id,g->assets+off,sizeof(g->assets)-off,&view)==DOK_ASSET_OK);
            CHECK(dok_level_parse(&view,&g->scratch.world.level,&g->world.base)==DOK_WORLD_OK);
            CHECK(dok_presentation_bind(&g->world.effective,&g->world.base,&session,&g->scratch.presentation,&g->world.presentation)==DOK_BALANCE_OK);off=(off+view.entry.bytes+31u)&~31u;}
        CHECK(dok_sprite_assets_load(&g->world.presentation,&g->bank,g->assets+off,sizeof(g->assets)-off)==DOK_STATUS_OK);s->initial=g->world.initial;
        CHECK(bind(s)==DOK_STATUS_OK);CHECK(s->ai.count>0&&s->ai.count<=DOK_AI_LIMIT);
        for(uint32_t i=0;i<s->ai.count;++i){dok_enemy *e=&s->ai.enemies[i];const dok_level_object *o=&g->world.effective.objects[g->world.initial.glyph_index[e->glyph]-1u];
            CHECK(e->health==dok_level_field_at(o,0xa4)->value&&e->motion.speed==dok_level_field_at(o,0x38)->value);
            if(level==1u&&(e->glyph==0x81u||e->glyph==0x86u))CHECK(e->health==42&&e->motion.speed==20&&dok_level_field_at(o,0xba)->value==9000);
            if(level==1u&&e->glyph==0x8cu)CHECK(e->health==12);
            if(level==1u&&e->glyph==0xa0u)CHECK(e->health==54&&e->motion.speed==60&&o->width_parameter==7&&dok_level_field_at(o,0xba)->value==3400);
        }
        /* First hit does not reduce sleeping HP; same source event then does. */
        for(uint32_t i=0;i<s->ai.count;++i){dok_enemy *e=&s->ai.enemies[i];dok_world_object_seed *seed=&g->world.initial.objects[g->world.initial.glyph_index[e->glyph]-1u];uint32_t accepted;
            if(!seed->pose_known)continue;int16_t hp=e->health;
            CHECK(dok_ai_hit(&s->ai,seed->reference,g->world.initial.hero,1,&accepted)==DOK_STATUS_OK&&accepted&&e->health==hp&&e->state==1);
            CHECK(dok_ai_hit(&s->ai,seed->reference,g->world.initial.hero,1,&accepted)==DOK_STATUS_OK&&accepted&&e->health==hp-1&&e->state==3);break;
        }
        CHECK(dok_combat_clear(&g->combat)==DOK_STATUS_OK);CHECK(dok_ai_clear(&s->ai)==DOK_STATUS_OK);
        uint32_t hashes[2]={2166136261u,2166136261u};
        for(uint32_t cadence=0;cadence<2u;++cadence){
            CHECK(bind(s)==DOK_STATUS_OK);++r->scenarios;uint32_t next_hero=0,ordinal=0;
            for(uint32_t t=0;t<=120000u;t+=20u){
                g->ticks=t;
                if(t>=next_hero){CHECK(hero_service(s,ordinal++)==DOK_STATUS_OK);next_hero+=416u;}
                CHECK(sweep(s,t)==DOK_STATUS_OK&&!g->fault&&!s->ai.fault);
                if(t%(cadence?160u:320u)==0u){dok_nds_render_camera camera={s->hero.x,s->hero.y,s->hero.z,0,0};const dok_sprite_list *list;
                    CHECK(dok_render_trig((uint16_t)s->hero.heading,&camera.sin_q15,&camera.cos_q15));
                    dok_sprite_snapshot snapshot={g->world.initial.epoch,1,g->bank.bindings,&g->world.initial};
                    CHECK(dok_sprites_gather(&snapshot,&camera,(uint16_t)s->hero.heading,&g->list,&list)==DOK_STATUS_OK);}
                if(t%416u==0u){hashes[cadence]=hash(hashes[cadence],s->ai.enemies,s->ai.count*sizeof(dok_enemy));
                    hashes[cadence]=hash(hashes[cadence],g->world.initial.maps,sizeof(g->world.initial.maps));}
            }
            CHECK(s->ai.callbacks>0&&s->ai.considerations>0&&s->ai.motion_callbacks>0);
            r->callbacks+=s->ai.callbacks;r->considerations+=s->ai.considerations;r->motion_callbacks+=s->ai.motion_callbacks;r->course_trials+=s->ai.course_trials;
            CHECK(dok_combat_clear(&g->combat)==DOK_STATUS_OK);CHECK(dok_ai_clear(&s->ai)==DOK_STATUS_OK);CHECK(g->scheduler._event_count==0);
        }
        CHECK(hashes[0]==hashes[1]);if(language==1u)language_hash[level-1u]=hashes[0];else CHECK(language_hash[level-1u]==hashes[0]);
        r->trace_hash=(r->trace_hash^hashes[0])*16777619u;
        /* Explicit all-awake group/corridor stress, never a production rule. */
        CHECK(bind(s)==DOK_STATUS_OK);++r->scenarios;s->profile_stress=1;
        for(uint32_t i=0;i<s->ai.count;++i){dok_enemy *e=&s->ai.enemies[i];const dok_world_object_seed *seed=&g->world.initial.objects[g->world.initial.glyph_index[e->glyph]-1u];
            if(seed->pose_known)CHECK(dok_ai_consider(&s->ai,seed->reference,e->motion.x,e->motion.z)==DOK_STATUS_OK);}
        for(uint32_t t=0;t<=12000u;t+=20u)CHECK(sweep(s,t)==DOK_STATUS_OK&&!g->fault&&!s->ai.fault);
        for(uint32_t i=0;i<s->ai.count;++i){dok_enemy *e=&s->ai.enemies[i];const dok_world_object_seed *seed=&g->world.initial.objects[g->world.initial.glyph_index[e->glyph]-1u];
            if(!seed->pose_known)continue;int32_t margin=g->world.effective.settings[7],side=g->world.effective.settings[3];
            CHECK(e->motion.x>=margin&&e->motion.z>=margin&&e->motion.x+margin<g->world.effective.settings[0]*side&&e->motion.z+margin<g->world.effective.settings[2]*side);
            uint32_t cell=(uint32_t)(e->motion.z/side)*(uint16_t)g->world.effective.settings[0]+(uint32_t)(e->motion.x/side);
            CHECK(g->world.initial.maps[DOK_WORLD_MMAP][cell]==e->glyph);}
        r->stress_callbacks+=s->ai.callbacks;
        CHECK(dok_combat_clear(&g->combat)==DOK_STATUS_OK);CHECK(dok_ai_clear(&s->ai)==DOK_STATUS_OK);s->profile_stress=0;
    }
    nds_asset_file_close(&file);return 0;
}
