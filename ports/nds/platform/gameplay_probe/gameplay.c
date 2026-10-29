#include "gameplay.h"
#include "effects_runtime.h"
#include <string.h>
#if defined(ARM9)
#include <nds.h>
#endif
#define HERO_CALLBACK 0x1701u
static int trig(void *p,uint32_t h,int32_t *s,int32_t *c)
{(void)p;return dok_render_trig(h,s,c);}
static dok_status hit(void *p,dok_object_ref t,dok_object_ref s,int16_t d,uint32_t *a)
{return dok_ai_hit(&((NdsGameplay *)p)->ai,t,s,d,a);}
static void combat_output(void *p,const dok_combat_output *o)
{NdsGameplay *g=p;if(o->kind<16u)++g->outputs[o->kind];}
static void ai_output(void *p,const dok_ai_output *o)
{NdsGameplay *g=p;if(o->kind<16u)++g->ai_outputs[o->kind];}
static void item_output(void *p,const dok_item_output *o)
{NdsGameplay *g=p;(void)o;++g->outputs[10];}
static void interaction_output(void *p,const dok_interaction_output *o)
{
    NdsGameplay *g=p;++g->outputs[11];
    if(o->kind==DOK_INTERACTION_STAT){
        if(g->pending_count==4u){g->fault=1;return;}
        g->pending_category[g->pending_count]=o->category;
        g->pending_value[g->pending_count++]=o->value;
    }
}
static dok_status displace(void *p,dok_object_ref a,dok_motion_state *m,int32_t x,int32_t z)
{dok_collision_report report;return dok_collision_displace(&((NdsGameplay *)p)->collision,a,m,x,z,&report);}
static void publish_hero(NdsGameplay *g)
{
    dok_world_object_seed *s=&g->items.world->objects[g->items.world->hero.object_id-1u];
    s->x=g->hero.x;s->y=g->hero.y;s->z=g->hero.z;s->heading=g->hero.heading;
}
dok_status nds_gameplay_pose(NdsGameplay *g,int32_t x,int32_t y,int32_t z,int16_t h)
{
    dok_status s;dok_motion_state m=g->hero;
    if((s=displace(g,g->items.world->hero,&m,x,z))!=DOK_STATUS_OK)return s;
    m.y=y;m.heading=h;g->hero=m;publish_hero(g);return DOK_STATUS_OK;
}
dok_status nds_gameplay_bind(NdsGameplay *g,NdsEffectsRuntime *r)
{
    dok_status s;memset(g,0,sizeof(*g));r->game=g;g->c2=4;
    dok_initial_world *w=&r->world->initial;
    dok_combat_services combat={g,combat_output,hit,trig};
    dok_ai_services ai={g,trig,ai_output};
    dok_interaction_services doors={g,interaction_output,g,displace,g,trig};
#define DO(e) do{if((s=(e))!=DOK_STATUS_OK)return s;}while(0)
    DO(dok_collision_bind(&g->collision,w));
    DO(dok_items_bind(&g->items,w,&g->collision,item_output,g));
    DO(dok_combat_bind(&g->combat,&g->items,&r->scheduler,&r->world->presentation,r->sprites,&combat));
    DO(dok_combat_register(&g->combat));
    DO(dok_ai_bind(&g->ai,w,&g->collision,&r->scheduler,&r->world->presentation,r->sprites,&g->combat,&ai));
    DO(dok_ai_register(&g->ai));
    DO(dok_interactions_bind(&g->interactions,w,&g->collision,&r->scheduler,&doors));
    DO(dok_interactions_register(&g->interactions));
    DO(dok_controls_initialize(&g->controls));
    dok_world_object_seed *hero=&w->objects[w->hero.object_id-1u];
    DO(dok_motion_init(&g->hero,hero->x,hero->y,hero->z,hero->heading,5,120));
    dok_object_ref owner={w->epoch,0x40000u,1};
    DO(dok_events_register(&r->scheduler,owner,4));
    DO(dok_events_schedule(&r->scheduler,owner,HERO_CALLBACK,0,416));
    return DOK_STATUS_OK;
}
dok_status nds_gameplay_input(NdsGameplay *g,uint32_t held)
{
    dok_input_frame f={0};f.sequence=++g->input_sequence;f.held=held;
    f.pressed=held&~g->input_held;f.released=g->input_held&~held;g->input_held=held;
    if(f.pressed||f.released)g->input_pending=1;
    return dok_controls_feed(&g->controls,&f);
}
static dok_status hero_service(NdsGameplay *g,NdsEffectsRuntime *r)
{
    dok_status s,end;uint32_t now,accepted;dok_control_commands commands;
    dok_collision_context context;dok_collision_report cr;dok_motion_report mr;
    dok_nds_render_camera camera={g->hero.x,g->hero.y,g->hero.z,0,0};
    DO(r->scheduler._config.read_ticks(r->scheduler._config.clock_context,&now));
    uint32_t elapsed=g->started?now-g->last_hero:0;
    DO(dok_interactions_cadence(g->c2,elapsed?elapsed:416u,&g->c2));
    if(!trig(g,(uint16_t)g->hero.heading,&camera.sin_q15,&camera.cos_q15))return DOK_STATUS_INVALID_STATE;
    DO(dok_ai_consider_camera(&g->ai,&camera,(uint16_t)g->hero.heading));
    if(r->simulation_list){
        dok_render_gather_bounds bounds;const dok_sprite_list *list;
        dok_sprite_snapshot snapshot={g->items.world->epoch,g->hero_callbacks+1u,r->sprites->bindings,g->items.world};
        if(dok_render_bounds(g->items.world,&camera,(uint16_t)g->hero.heading,&bounds)!=DOK_RENDER_SCENE_OK)return DOK_STATUS_INVALID_STATE;
        DO(dok_sprites_gather_live(&snapshot,g->collision.flags,g->ai.corpses,&camera,(uint16_t)g->hero.heading,r->simulation_list,&list));
        DO(nds_effects_arm_bounds(r,&bounds,list));
    }
    DO(dok_controls_take(&g->controls,&commands));
#if defined(ARM9)
    if(g->input_pending){uint32_t latency=cpuGetTiming()-g->input_tick;if(latency>g->input_latency_max)g->input_latency_max=latency;g->input_pending=0;}
#endif
    DO(dok_items_pickup(&g->items,&g->hero,&accepted));
    DO(dok_interactions_floor(&g->interactions,g->items.world->hero,&g->hero,g->items.inventory.score,g->c2));
    uint32_t bits=commands.native_commands;
    DO(dok_interactions_use(&g->interactions,g->items.world->hero,&g->hero,&bits,g->items.inventory.keys,g->c2));
    for(uint32_t i=0;i<g->pending_count;++i)DO(dok_items_stat(&g->items,g->pending_category[i],g->pending_value[i],&accepted));
    g->pending_count=0;
    DO(dok_combat_controls(&g->combat,&commands));g->hero.flags=bits;
    if(g->items.inventory.health>0){
        DO(dok_collision_begin(&g->collision,g->items.world->hero,&g->hero,elapsed,&context));
        dok_motion_services services={g,trig,&context,dok_collision_move};
        s=dok_motion_update(&g->hero,elapsed,&services,&mr);end=dok_collision_end(&context,&cr);
        if(s!=DOK_STATUS_OK)return s;
        if(end!=DOK_STATUS_OK)return end;
        publish_hero(g);
    }
    DO(r->scheduler._config.read_ticks(r->scheduler._config.clock_context,&g->last_hero));
    g->started=1;++g->hero_callbacks;return DOK_STATUS_OK;
}
dok_status nds_gameplay_callback(NdsGameplay *g,NdsEffectsRuntime *r,dok_object_ref owner,uint16_t kind,uint32_t parameter)
{
    dok_status s;
#if defined(ARM9)
    uint32_t start=cpuGetTiming();
#endif
    if(kind==HERO_CALLBACK)s=hero_service(g,r);
    else if(kind==DOK_AI_CALLBACK)s=dok_ai_callback(&g->ai,owner,kind);
    else if(kind>=DOK_COMBAT_PROJECTILE&&kind<=DOK_COMBAT_SWITCH)s=dok_combat_callback(&g->combat,owner,kind,parameter);
    else s=dok_interactions_callback(&g->interactions,owner,kind);
#if defined(ARM9)
    uint32_t dt=cpuGetTiming()-start;g->callback_ticks+=dt;
    if(kind==HERO_CALLBACK)g->hero_ticks+=dt;else if(kind==DOK_AI_CALLBACK)g->ai_ticks+=dt;else g->combat_ticks+=dt;
    if(dt>g->max_service_ticks)g->max_service_ticks=dt;
#endif
    if(s!=DOK_STATUS_OK)g->fault=(uint32_t)s;
    return s;
}
