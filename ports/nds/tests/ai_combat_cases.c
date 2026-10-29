#include "ai_combat_cases.h"
#include "../platform/assets/file_reader.h"
#include <string.h>
#define CHECK(e) do{++r->checks;if(!(e)){++r->failures;if(!r->line)r->line=__LINE__;return 1;}}while(0)
static dok_status clock_now(void *p,uint32_t *out)
{*out=((dok_ai_combat_cases_workspace *)p)->simulation.game.ticks;return DOK_STATUS_OK;}
static int trig(void *p,uint32_t h,int32_t *sn,int32_t *cs){(void)p;return dok_render_trig(h,sn,cs);}
static void ai_output(void *p,const dok_ai_output *o)
{dok_ai_combat_cases_workspace *s=p;if(o->kind<11u)++s->notices[o->kind];}
static void combat_output(void *p,const dok_combat_output *o)
{dok_ai_combat_cases_workspace *s=p;if(o->kind<9u)++s->combat_notices[o->kind];}
static dok_status hit(void *p,dok_object_ref target,dok_object_ref source,int16_t damage,uint32_t *accepted)
{return dok_ai_hit(&((dok_ai_combat_cases_workspace *)p)->simulation.ai,target,source,damage,accepted);}
static void callback(void *p,dok_event_scheduler *scheduler,dok_object_ref owner,uint16_t kind,uint32_t parameter)
{
    dok_ai_combat_cases_workspace *s=p;dok_ai *ai=&s->simulation.ai;dok_combat_cases_workspace *g=&s->simulation.game;dok_status status;(void)scheduler;
    if(kind==DOK_AI_CALLBACK){
        uint32_t n=ai->glyph_index[owner.object_id-0x30000u];if(!n){g->fault=1;return;}dok_enemy *e=&ai->enemies[n-1u];
        dok_ai_combat_trace t={0};t.language=(int32_t)s->language;t.level=(int32_t)s->level;t.glyph=e->glyph;t.scenario=(int32_t)s->scenario;
        t.state_in=e->state;t.health_in=e->health;t.next_in=e->next_cursor;t.kind_in=e->displayed_kind;t.cursor_in=e->displayed_cursor;
        t.course_in=e->course_counter;t.shots_in=e->shots;t.elapsed=(int32_t)(g->ticks-e->burst_started);t.front_class=(int32_t)s->front_class;t.visible=(int32_t)s->visible;
        t.flags_in=g->collision.flags[e->glyph];t.height=g->world.effective.objects[g->world.initial.glyph_index[e->glyph]-1u].height_parameter;t.hero_in=g->items.inventory.health;
        t.cadence=(int32_t)s->cadence;
        uint32_t melee=s->notices[DOK_AI_MELEE],ranged=s->notices[DOK_AI_RANGED],dead=s->notices[DOK_AI_DEAD];
        status=dok_ai_callback(ai,owner,kind);
        t.state_out=e->state;t.health_out=e->health;t.next_out=e->next_cursor;t.kind_out=e->displayed_kind;t.cursor_out=e->displayed_cursor;
        t.course_out=e->course_counter;t.shots_out=e->shots;t.flags_out=g->collision.flags[e->glyph];t.corpse_out=e->corpse;t.hero_out=g->items.inventory.health;
        t.melee=(int32_t)(s->notices[DOK_AI_MELEE]-melee);t.ranged=(int32_t)(s->notices[DOK_AI_RANGED]-ranged);t.dead=(int32_t)(s->notices[DOK_AI_DEAD]-dead);
        const dok_event_owner *event;
        if(dok_events_inspect_owner(&g->scheduler,owner,&event)==DOK_STATUS_OK&&event->count)t.interval=(int32_t)event->events[0].interval;
        if(e->corpse){uint32_t cell=(uint32_t)(e->motion.z/480)*(uint16_t)g->world.effective.settings[0]+(uint32_t)(e->motion.x/480);
            t.plane=g->world.initial.maps[DOK_WORLD_TMAP][cell]==e->glyph?1:0;}else t.plane=-1;
        uint32_t values[33];_Static_assert(sizeof(t)==sizeof(values),"N17 scalar trace ABI");memcpy(values,&t,sizeof(t));
        for(uint32_t i=0;i<sizeof(t)/4u;++i){uint32_t value=values[i];
            for(uint32_t b=0;b<4u;++b){uint32_t byte=(value>>(8u*b))&255u;s->trace_hash=(s->trace_hash^byte)*16777619u;
                if(i&&i+1u<sizeof(t)/4u){uint32_t *h=&s->functional_hash[s->cadence][s->language-1u];*h=(*h^byte)*16777619u;}}}
        ++s->trace_count;if(s->trace)s->trace(s->trace_context,&t);
    }else status=dok_combat_callback(&g->combat,owner,kind,parameter);
    if(status!=DOK_STATUS_OK)g->fault=(uint32_t)status;
}
static dok_status bind(dok_ai_combat_cases_workspace *s,uint32_t index,uint32_t contact,uint32_t occupied)
{
    dok_ai_cases_workspace *sim=&s->simulation;dok_combat_cases_workspace *g=&sim->game;dok_status r;
    g->world.initial=sim->initial;g->ticks=100000;g->fault=0;
    memset(s->notices,0,sizeof(s->notices));memset(s->combat_notices,0,sizeof(s->combat_notices));
    dok_initial_world *w=&g->world.initial;memset(w->maps,' ',3u*DOK_LEVEL_MAX_CELLS);
    uint32_t width=(uint16_t)w->definition->settings[0],length=(uint16_t)w->definition->settings[2],wall=0,item=0;
    for(uint32_t i=0;i<w->object_count;++i){const dok_level_object *o=&w->definition->objects[i];if(o->class_id==2u&&!wall)wall=o->glyph;if(o->class_id==3u&&!item)item=o->glyph;}
    if(!wall||!item)return DOK_STATUS_INVALID_STATE;
    for(uint32_t z=0;z<length;++z)for(uint32_t x=0;x<width;++x)if(!x||!z||x+1u==width||z+1u==length)w->maps[DOK_WORLD_TMAP][z*width+x]=(unsigned char)wall;
    dok_world_object_seed *target=&w->objects[index],*hero=&w->objects[w->hero.object_id-1u];
    target->x=2640;target->z=2160;target->pose_known=1;target->heading=0;
    hero->x=2640;hero->z=contact?2640:6000;hero->heading=180;
    w->maps[DOK_WORLD_MMAP][4u*width+5u]=w->definition->objects[index].glyph;
    w->maps[DOK_WORLD_MMAP][(uint32_t)(hero->z/480)*width+5u]=w->definition->objects[w->hero.object_id-1u].glyph;
    if(occupied)w->maps[DOK_WORLD_TMAP][4u*width+5u]=(unsigned char)item;
    dok_event_config config={s,clock_now,s,callback,w->epoch};dok_combat_services combat={s,combat_output,hit,trig};dok_ai_services services={s,trig,ai_output};
    if((r=dok_collision_bind(&g->collision,w))!=DOK_STATUS_OK)return r;
    if((r=dok_items_bind(&g->items,w,&g->collision,NULL,NULL))!=DOK_STATUS_OK)return r;
    if((r=dok_events_init(&g->scheduler,&g->events,&config))!=DOK_STATUS_OK)return r;
    if((r=dok_combat_bind(&g->combat,&g->items,&g->scheduler,&g->world.presentation,&g->bank,&combat))!=DOK_STATUS_OK)return r;
    if((r=dok_combat_register(&g->combat))!=DOK_STATUS_OK)return r;
    if((r=dok_ai_bind(&sim->ai,w,&g->collision,&g->scheduler,&g->world.presentation,&g->bank,&g->combat,&services))!=DOK_STATUS_OK)return r;
    if((r=dok_ai_register(&sim->ai))!=DOK_STATUS_OK)return r;
    dok_enemy *e=&sim->ai.enemies[sim->ai.glyph_index[w->definition->objects[index].glyph]-1u];e->motion.speed=0;
    dok_presentation_value v;if(dok_presentation_get(&g->world.presentation,e->glyph,DOK_PRESENT_ANIMATION,&v)!=DOK_BALANCE_OK||dok_sprite_asset_select(&g->bank,e->glyph,v.asset_id,0)!=DOK_STATUS_OK)return DOK_STATUS_INVALID_STATE;
    s->front_class=contact?1u:0u;s->visible=1;s->render_next=g->ticks+(s->cadence?166u:333u);return DOK_STATUS_OK;
}
static const dok_sprite_animation *sequence(dok_ai_combat_cases_workspace *s,uint8_t glyph,uint16_t kind)
{
    dok_presentation_value value;dok_combat_cases_workspace *g=&s->simulation.game;
    if(dok_presentation_get(&g->world.presentation,glyph,kind,&value)!=DOK_BALANCE_OK)return NULL;
    for(uint32_t i=0;i<g->bank.animation_count;++i)if(g->bank.animations[i]==value.asset_id)return &g->bank.sequences[i];return NULL;
}
static dok_status sweep(dok_ai_combat_cases_workspace *s,uint32_t now)
{dok_event_dispatch_report report;s->simulation.game.ticks=now;return dok_events_dispatch(&s->simulation.game.scheduler,&report);}
static dok_status observer(dok_ai_combat_cases_workspace *s)
{
    dok_combat_cases_workspace *g=&s->simulation.game;if(g->ticks<s->render_next)return DOK_STATUS_OK;
    dok_nds_render_camera camera={2640,-300,2880,0,-32768};const dok_sprite_list *list;
    dok_sprite_snapshot snapshot={g->world.initial.epoch,1,g->bank.bindings,&g->world.initial};
    dok_status r=dok_sprites_gather_live(&snapshot,g->collision.flags,s->simulation.ai.corpses,&camera,180,&g->list,&list);
    if(r!=DOK_STATUS_OK)return r;
    ++s->observers;s->render_next=g->ticks+(s->cadence?166u:333u);return DOK_STATUS_OK;
}
static dok_status due(dok_ai_combat_cases_workspace *s,dok_enemy *e)
{
    dok_combat_cases_workspace *g=&s->simulation.game;const dok_event_owner *o;dok_object_ref owner={g->world.initial.epoch,0x30000u+e->glyph,1};dok_status r;
    if((r=dok_events_inspect_owner(&g->scheduler,owner,&o))!=DOK_STATUS_OK||!o->count)return DOK_STATUS_INVALID_STATE;
    uint32_t deadline=o->events[0].deadline,callbacks=s->simulation.ai.callbacks;
    /* Same20-tick service input with independent30/60 observation. Artificial
     * burst-boundary clock jumps are separate fixtures, not rendered stalls. */
    if(s->render_next<g->ticks)s->render_next=g->ticks;
    while(deadline>g->ticks+1u){uint32_t t=deadline-1u;if(t-g->ticks>20u)t=g->ticks+20u;
        if((r=sweep(s,t))!=DOK_STATUS_OK)return r;if(s->simulation.ai.callbacks!=callbacks)return DOK_STATUS_INVALID_STATE;
        if((r=observer(s))!=DOK_STATUS_OK)return r;}
    uint32_t t=deadline>g->ticks?deadline:g->ticks;
    for(uint32_t i=0;i<4u;++i){if((r=sweep(s,t))!=DOK_STATUS_OK)return r;if(s->simulation.ai.callbacks>callbacks)return DOK_STATUS_OK;}
    return DOK_STATUS_INVALID_STATE;
}
static int same_kind(const dok_level_definition *d,const dok_level_object *a,const dok_level_object *b)
{
    if(a->class_id!=b->class_id||a->flags!=b->flags||a->width_parameter!=b->width_parameter||a->height_parameter!=b->height_parameter||a->field_count!=b->field_count)return 0;
    if(!dok_level_span_equal(d,a->animation,d->source+b->animation.offset,b->animation.length))return 0;
    for(uint32_t i=0;i<a->field_count;++i){const dok_level_field *f=&a->fields[i],*g=&b->fields[i];
        if(f->native_offset!=g->native_offset||f->kind!=g->kind)return 0;
        if(f->native_offset==0x32||f->kind==DOK_LEVEL_TEXT)continue;
        if(f->kind==DOK_LEVEL_WORD||f->kind==DOK_LEVEL_LONG||f->kind==DOK_LEVEL_CHARACTER){if(f->value!=g->value)return 0;}
        else if(!dok_level_span_equal(d,f->token,d->source+g->token.offset,g->token.length))return 0;
    }return 1;
}
static int finish(dok_ai_combat_cases_workspace *s,dok_ai_combat_cases_report *r)
{
    dok_combat_cases_workspace *g=&s->simulation.game;r->callbacks+=s->simulation.ai.callbacks;r->deaths+=s->notices[DOK_AI_DEAD];r->melee+=s->notices[DOK_AI_MELEE];r->ranged+=s->notices[DOK_AI_RANGED];
    CHECK(dok_combat_clear(&g->combat)==DOK_STATUS_OK);CHECK(dok_ai_clear(&s->simulation.ai)==DOK_STATUS_OK);CHECK(!g->scheduler._event_count&&!g->collision._projectile_occupied);return 0;
}
int dok_ai_combat_cases(dok_ai_combat_cases_workspace *s,dok_ai_combat_cases_report *r,const char *path)
{
    dok_ai_cases_workspace *sim=&s->simulation;dok_combat_cases_workspace *g=&sim->game;
    NdsAssetFile file;dok_asset_reader reader;dok_asset_session session;uint32_t language,level;
    memset(r,0,sizeof(*r));s->trace_hash=2166136261u;s->trace_count=s->observers=0;
    for(uint32_t i=0;i<2u;++i)for(uint32_t j=0;j<2u;++j)s->functional_hash[i][j]=2166136261u;
    CHECK(nds_asset_file_open(&file,path,&reader)==DOK_ASSET_OK);CHECK(dok_assets_open(&reader,&g->io,&session)==DOK_ASSET_OK);
    for(s->cadence=0;s->cadence<2u;++s->cadence)for(language=1;language<=2u;++language)for(level=1;level<=3u;++level){
        s->language=language;s->level=level;
        CHECK(dok_render_world_load(&session,level,level,&g->world,&g->scratch,g->assets,sizeof(g->assets))==DOK_STATUS_OK);
        uint32_t off=(g->world.asset_bytes+31u)&~31u;
        if(language==2u){dok_asset_view view;dok_asset_id id=(UINT32_C(2)<<30)|((17u+level)<<18)|((uint32_t)DOK_ASSET_LEVEL_CP866<<10);
            CHECK(dok_assets_load(&session,id,g->assets+off,sizeof(g->assets)-off,&view)==DOK_ASSET_OK);
            CHECK(dok_level_parse(&view,&g->scratch.world.level,&g->world.base)==DOK_WORLD_OK);
            CHECK(dok_presentation_bind(&g->world.effective,&g->world.base,&session,&g->scratch.presentation,&g->world.presentation)==DOK_BALANCE_OK);off=(off+view.entry.bytes+31u)&~31u;}
        CHECK(dok_sprite_assets_load(&g->world.presentation,&g->bank,g->assets+off,sizeof(g->assets)-off)==DOK_STATUS_OK);sim->initial=g->world.initial;
        for(uint32_t index=0;index<sim->initial.object_count;++index){const dok_level_object *o=&g->world.effective.objects[index];uint32_t duplicate=0;
            if(o->class_id!=9u||!sim->initial.objects[index].placements)continue;++r->placed_definitions;
            for(uint32_t j=0;j<index;++j)if(sim->initial.objects[j].placements&&same_kind(&g->world.effective,o,&g->world.effective.objects[j])){duplicate=1;break;}
            if(duplicate)continue;++r->groups;
            for(uint32_t plane=0;plane<2u;++plane){
                CHECK(bind(s,index,1,plane)==DOK_STATUS_OK);++r->scenarios;s->scenario=plane;
                dok_ai *ai=&sim->ai;dok_enemy *e=&ai->enemies[ai->glyph_index[o->glyph]-1u];dok_object_ref target=g->world.initial.objects[index].reference;uint32_t accepted;
                int16_t hp=e->health;CHECK(dok_ai_hit(ai,target,g->world.initial.hero,32767,&accepted)==DOK_STATUS_OK&&accepted&&e->health==hp);
                const dok_sprite_animation *attack=sequence(s,e->glyph,0xe8),*death=sequence(s,e->glyph,0xec);CHECK(attack&&death);
                for(uint32_t frame=0;frame<attack->count;++frame){CHECK(due(s,e)==DOK_STATUS_OK&&!g->fault);CHECK(e->state==2&&e->displayed_kind==1&&e->displayed_cursor==(int16_t)frame&&g->items.inventory.health==100);}
                CHECK(due(s,e)==DOK_STATUS_OK&&!g->fault&&g->items.inventory.health==95&&s->notices[DOK_AI_MELEE]==1&&e->displayed_cursor==0);
                CHECK(dok_ai_hit(ai,target,g->world.initial.hero,0,&accepted)==DOK_STATUS_OK&&accepted&&e->state==3);
                CHECK(due(s,e)==DOK_STATUS_OK&&!g->fault&&e->state==2&&e->displayed_kind==2&&e->displayed_cursor==0);
                dok_object_ref projectile;CHECK(dok_combat_spawn(&g->combat,target,'3',e->motion.x,e->motion.z,270,&projectile)==DOK_STATUS_OK);
                CHECK(dok_ai_hit(ai,target,g->world.initial.hero,e->health,&accepted)==DOK_STATUS_OK&&accepted&&e->health==0);
                CHECK(dok_ai_hit(ai,target,g->world.initial.hero,1,&accepted)==DOK_STATUS_OK&&!accepted);
                for(uint32_t frame=0;frame<death->count;++frame){
                    CHECK(due(s,e)==DOK_STATUS_OK&&!g->fault);CHECK(e->state==4&&!e->corpse&&e->displayed_kind==2&&e->displayed_cursor==(int16_t)frame);
                    dok_sprite_frame f;CHECK(dok_sprite_animation_get(death,(int32_t)frame,&f)==DOK_STATUS_OK);if(f.interval<0)++r->negative_intervals;
                }
                CHECK(due(s,e)==DOK_STATUS_OK&&!g->fault&&e->corpse&&s->notices[DOK_AI_DEAD]==1);
                uint32_t width=(uint16_t)g->world.effective.settings[0],cell=4u*width+5u;
                CHECK((g->collision.flags[e->glyph]&0x410u)==0x10u);
                CHECK(g->world.initial.maps[plane?DOK_WORLD_MMAP:DOK_WORLD_TMAP][cell]==e->glyph);
                CHECK(g->combat.projectiles[projectile.object_id-257u].phase!=0);
                uint32_t callbacks=ai->callbacks;CHECK(sweep(s,g->ticks+1u)==DOK_STATUS_OK&&ai->callbacks==callbacks&&s->notices[DOK_AI_DEAD]==1);
                dok_nds_render_camera camera={2640,-300,2880,0,-32768};const dok_sprite_list *list;
                dok_sprite_snapshot snapshot={g->world.initial.epoch,1,g->bank.bindings,&g->world.initial};
                CHECK(dok_sprites_gather_live(&snapshot,g->collision.flags,ai->corpses,&camera,180,&g->list,&list)==DOK_STATUS_OK);
                uint32_t seen=0;for(uint32_t j=0;j<list->count;++j)if(list->poses[j].glyph==e->glyph){++seen;CHECK(list->poses[j].y==0&&(list->poses[j].flags&16u));}CHECK(seen>0);
                g->items.inventory.health=1;CHECK(dok_combat_hurt_hero(&g->combat,target,5,&accepted)==DOK_STATUS_OK&&accepted&&g->items.inventory.health==0);
                uint32_t deaths=s->combat_notices[DOK_COMBAT_HERO_DEAD];CHECK(dok_combat_hurt_hero(&g->combat,target,5,&accepted)==DOK_STATUS_OK&&s->combat_notices[DOK_COMBAT_HERO_DEAD]==deaths);
                for(uint32_t j=0;j<g->world.initial.object_count;++j)if(g->world.effective.objects[j].class_id==10u&&dok_level_field_at(&g->world.effective.objects[j],0x81)->value==1){g->items.inventory.weapons[1]=g->world.initial.objects[j].reference;break;}
                g->items.inventory.selected=g->items.inventory.pending=1;g->items.inventory.charge=100;g->combat.weapons[1].state=7;
                dok_control_commands fire={0};fire.native_commands=DOK_INPUT_NATIVE_FIRE;uint32_t active=g->combat.active;
                CHECK(dok_combat_controls(&g->combat,&fire)==DOK_STATUS_OK&&g->combat.active==active);
                CHECK(finish(s,r)==0);
            }
            CHECK(bind(s,index,0,0)==DOK_STATUS_OK);++r->scenarios;s->scenario=2;
            dok_ai *ai=&sim->ai;dok_enemy *e=&ai->enemies[ai->glyph_index[o->glyph]-1u];dok_object_ref target=g->world.initial.objects[index].reference;uint32_t accepted;
            CHECK(dok_ai_hit(ai,target,g->world.initial.hero,32767,&accepted)==DOK_STATUS_OK&&accepted);
            uint32_t visible;CHECK(dok_ai_sight(ai,target,&visible)==DOK_STATUS_OK&&visible);
            int16_t delay=(int16_t)dok_level_field_at(o,0xa8)->value,limit=(int16_t)dok_level_field_at(o,0xa6)->value;
            uint32_t elapsed=(uint32_t)((uint16_t)delay+1u)*10000u;
            CHECK(sweep(s,100000u+elapsed-1u)==DOK_STATUS_OK&&!g->fault&&s->notices[DOK_AI_RANGED]==0);
            /* At the next admitted source callback the strict threshold has
             * passed. Keep empty-front fixtures by removing explicit shots. */
            for(uint32_t shot=0;shot<(uint32_t)(limit>0?limit:1);++shot){
                CHECK(due(s,e)==DOK_STATUS_OK&&!g->fault);
                if(limit>0)CHECK(s->notices[DOK_AI_RANGED]==shot+1u&&e->shots==(int16_t)(shot+1u));else CHECK(!s->notices[DOK_AI_RANGED]&&e->shots==0);
                CHECK(dok_combat_remove_owner(&g->combat,target)==DOK_STATUS_OK);
            }
            if(limit>0){CHECK(due(s,e)==DOK_STATUS_OK&&!g->fault&&e->shots==0);CHECK(dok_combat_remove_owner(&g->combat,target)==DOK_STATUS_OK);}
            /* Opaque TMap blocks the native double-advance sight. */
            uint32_t width=(uint16_t)g->world.effective.settings[0];unsigned char wall=g->world.initial.maps[DOK_WORLD_TMAP][0];
            g->world.initial.maps[DOK_WORLD_TMAP][7u*width+5u]=wall;s->visible=0;
            CHECK(dok_ai_sight(ai,target,&visible)==DOK_STATUS_OK&&!visible);
            g->world.initial.maps[DOK_WORLD_TMAP][7u*width+5u]=' ';
            g->world.initial.maps[DOK_WORLD_TMAP][5u*width+5u]=wall;
            g->world.initial.maps[DOK_WORLD_MMAP][5u*width+5u]=e->glyph;
            CHECK(dok_ai_sight(ai,target,&visible)==DOK_STATUS_OK&&visible);
            CHECK(finish(s,r)==0);
            if(limit>0)for(uint32_t guard=0;guard<3u;++guard){
                CHECK(bind(s,index,0,0)==DOK_STATUS_OK);++r->scenarios;s->scenario=3u+guard;s->front_class=guard==0?8u:guard==1?9u:0u;
                ai=&sim->ai;e=&ai->enemies[ai->glyph_index[o->glyph]-1u];target=g->world.initial.objects[index].reference;
                if(guard==0){dok_object_ref instance;
                    CHECK(dok_combat_spawn(&g->combat,g->world.initial.hero,'3',2640,2640,0,&instance)==DOK_STATUS_OK);
                    g->combat.projectiles[instance.object_id-257u].speed=0;
                    CHECK(sweep(s,100416)==DOK_STATUS_OK&&!g->fault);
                }else if(guard==1){uint32_t other=0;
                    for(uint32_t j=0;j<g->world.initial.object_count;++j)if(j!=index&&g->world.initial.objects[j].class_id==9u){other=g->world.effective.objects[j].glyph;break;}
                    CHECK(other!=0);g->world.initial.maps[DOK_WORLD_MMAP][5u*width+5u]=(unsigned char)other;
                }
                CHECK(dok_ai_hit(ai,target,g->world.initial.hero,32767,&accepted)==DOK_STATUS_OK&&accepted);
                if(guard==2){dok_object_ref instance;uint32_t count=0;
                    while(dok_combat_spawn(&g->combat,g->world.initial.hero,'3',2640,6000,0,&instance)==DOK_STATUS_OK){g->combat.projectiles[instance.object_id-257u].speed=0;CHECK(++count<=256u);}
                    CHECK(count>0&&count==g->combat.active);
                }
                CHECK(sweep(s,e->burst_started+elapsed)==DOK_STATUS_OK&&!g->fault);
                if(guard<2u)CHECK(e->shots==0&&!s->notices[DOK_AI_RANGED]);
                else CHECK(e->shots==1&&s->notices[DOK_AI_RANGED]==1);
                CHECK(finish(s,r)==0);
            }
            /* Early negative final interval preserves unsigned native deadline
             * wrap. The world can still be destroyed without a stale owner. */
            CHECK(bind(s,index,1,0)==DOK_STATUS_OK);s->scenario=0;
            ai=&sim->ai;e=&ai->enemies[ai->glyph_index[o->glyph]-1u];target=g->world.initial.objects[index].reference;
            const dok_sprite_animation *death=sequence(s,e->glyph,0xec);dok_sprite_frame last;
            CHECK(death&&dok_sprite_animation_get(death,(int32_t)death->count-1,&last)==DOK_STATUS_OK);
            if(last.interval<0){
                ++r->scenarios;g->ticks=0;s->render_next=s->cadence?166u:333u;
                CHECK(dok_ai_hit(ai,target,g->world.initial.hero,32767,&accepted)==DOK_STATUS_OK&&accepted);
                CHECK(dok_ai_hit(ai,target,g->world.initial.hero,e->health,&accepted)==DOK_STATUS_OK&&accepted);
                for(uint32_t frame=0;frame<death->count;++frame)CHECK(due(s,e)==DOK_STATUS_OK&&!g->fault);
                const dok_event_owner *event;dok_object_ref owner={g->world.initial.epoch,0x30000u+e->glyph,1};
                CHECK(dok_events_inspect_owner(&g->scheduler,owner,&event)==DOK_STATUS_OK&&event->count==1);
                CHECK(event->events[0].deadline==g->ticks+(uint32_t)(int32_t)last.interval&&event->events[0].deadline>g->ticks&&!e->corpse);
                CHECK(sweep(s,g->ticks+1000u)==DOK_STATUS_OK&&!e->corpse);
                CHECK(finish(s,r)==0);
            }else CHECK(finish(s,r)==0);
        }
    }
    CHECK(s->functional_hash[0][0]==s->functional_hash[0][1]);
    CHECK(s->functional_hash[0][0]==s->functional_hash[1][0]&&s->functional_hash[0][0]==s->functional_hash[1][1]);
    CHECK(s->observers>0);r->trace_hash=s->trace_hash;r->trace_count=s->trace_count;nds_asset_file_close(&file);return 0;
}
