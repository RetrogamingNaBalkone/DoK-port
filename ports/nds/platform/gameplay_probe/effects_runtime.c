#include "effects_runtime.h"
#include "gameplay.h"
#include <string.h>
#include <stddef.h>
#if defined(ARM9)
#include <nds.h>
#endif
static void trace(NdsEffectsRuntime *r,uint32_t kind,uint32_t glyph,uint32_t interval,uint32_t cancel)
{
    uint32_t index=r->trace_total%NDS_EFFECT_TRACE_CAPACITY;NdsEffectTrace row={0};
    row.magic=0x13454b44u;row.ordinal=++r->trace_total;row.sequence=row.ordinal*2u-1u;
    row.epoch=r->world->initial.epoch;row.case_id=r->case_id;row.frame_id=r->frame_id;
    row.source_ticks=r->last_ticks;row.kind=kind;row.glyph=glyph;row.interval=interval;row.cancel=cancel;
    row.counter=kind==13u?(uint32_t)r->water[glyph].counter:(uint32_t)r->players[glyph].cursor;
    row.displayed=kind==13u?(uint32_t)r->water[glyph].displayed_phase:r->players[glyph].displayed.image;
    r->traces[index].sequence=row.sequence;
#if defined(ARM9)
    DC_FlushRange((const void *)&r->traces[index].sequence,32);
#endif
    memcpy((void *)&r->traces[index],&row,sizeof(row));
#if defined(ARM9)
    DC_FlushRange((const void *)&r->traces[index],sizeof(row));
#endif
    ++r->traces[index].sequence;
#if defined(ARM9)
    DC_FlushRange((const void *)&r->traces[index].sequence,32);
#endif
}
static dok_status read_ticks(void *context,uint32_t *out)
{
    NdsEffectsRuntime *r=context;
    dok_status result=EFFECT_REAL_CLOCK?dok_world_clock_read(&r->clock,out):DOK_STATUS_OK;
    if(!EFFECT_REAL_CLOCK)*out=r->fixture_ticks;
    if(result==DOK_STATUS_OK)r->last_ticks=*out;
    return result;
}
static void callback(void *context,dok_event_scheduler *scheduler,dok_object_ref owner,uint16_t kind,uint32_t glyph)
{
    NdsEffectsRuntime *r=context;uint32_t cancel,interval=0;(void)owner;++r->callbacks;
    if(kind>=0x1100u) {if(nds_gameplay_callback(r->game,r,owner,kind,glyph)!=DOK_STATUS_OK)r->fault=7;return;}
    if(glyph>255u) {r->fault=1;return;}
    if(kind==13u) {
        ++r->water_callbacks;
        if(dok_water_step(&r->water[glyph],&cancel)!=DOK_STATUS_OK) {r->fault=2;return;}
        if(!cancel) {
            uint32_t material=r->world->bindings[glyph].images[DOK_GEOMETRY_FLOOR].material;
            if(!material) {r->fault=3;return;}
            r->material_phase[material-1u]=(uint16_t)r->water[glyph].displayed_phase;
        }
    } else if(kind==12u) {
        ++r->animation_callbacks;
        if(dok_sprite_player_step(&r->players[glyph],&interval,&cancel)!=DOK_STATUS_OK) {r->fault=4;return;}
        if(!cancel) {r->sprites->bindings[glyph].frame=r->players[glyph].displayed;r->sprites->bindings[glyph].flags=8;}
    } else {r->fault=5;return;}
    if((cancel?dok_events_cancel_current(scheduler):dok_events_set_interval(scheduler,interval))!=DOK_STATUS_OK)r->fault=6;
    trace(r,kind,glyph,interval,cancel);
}
dok_status nds_effects_load(NdsEffectsRuntime *r,dok_render_world *world,dok_sprite_asset_bank *sprites,
    const dok_asset_session *session,const dok_asset_reader *reader,dok_event_bank *events,
    const dok_platform_services *platform,unsigned char *pool,uint32_t capacity)
{
    dok_presentation_value value;dok_asset_view view;unsigned char candidate[768];uint32_t i;
    if(!r || !world || !sprites || !session || !reader || !events || !platform || !pool)
        return DOK_STATUS_INVALID_ARGUMENT;
    memset(r,0,offsetof(NdsEffectsRuntime,traces));r->world=world;r->sprites=sprites;r->percentage=99;
    if(dok_presentation_get(&world->presentation,0,DOK_PRESENT_LIGHT,&value)!=DOK_BALANCE_OK ||
       dok_assets_load(session,value.asset_id,r->light,sizeof(r->light),&view)!=DOK_ASSET_OK ||
       dok_presentation_get(&world->presentation,0,DOK_PRESENT_PALETTE,&value)!=DOK_BALANCE_OK ||
       dok_effects_palette_load(session,reader,value.asset_id,candidate,r->rgb6)!=DOK_STATUS_OK ||
       dok_light_palette(r->rgb6,r->light,99,r->palette)!=DOK_STATUS_OK ||
       dok_world_clock_init(&r->clock,platform,0)!=DOK_STATUS_OK)
        return DOK_STATUS_INVALID_STATE;
    for(i=0;i<16u;++i)if(r->light[i*256u])return DOK_STATUS_UNSUPPORTED;
    dok_event_config config={r,read_ticks,r,callback,world->initial.epoch};
    if(dok_events_init(&r->scheduler,events,&config)!=DOK_STATUS_OK)return DOK_STATUS_INVALID_STATE;
    dok_effect_cache_init(&r->cache,pool,capacity);
    for(i=0;i<world->effective.object_count;++i) {
        const dok_level_object *o=&world->effective.objects[i];uint32_t glyph=o->glyph;
        if(o->class_id==12u) {
            uint32_t m=world->bindings[glyph].images[DOK_GEOMETRY_FLOOR].material;
            if(!m)continue;
            r->material_water[m-1u]=1;r->material_flags[m-1u]=world->bindings[glyph].images[DOK_GEOMETRY_FLOOR].flags;
            r->water[glyph]=(dok_water_state){18,0,1};
            if(dok_events_register(&r->scheduler,world->initial.objects[i].reference,4)!=DOK_STATUS_OK)return DOK_STATUS_INVALID_STATE;
        } else if((o->class_id==3u || o->class_id==6u || o->class_id==10u) && (o->flags&0x0800u)) {
            if(dok_presentation_get(&world->presentation,(uint8_t)glyph,DOK_PRESENT_ANIMATION,&value)!=DOK_BALANCE_OK)return DOK_STATUS_INVALID_STATE;
            for(uint32_t a=0;a<sprites->animation_count;++a)if(sprites->animations[a]==value.asset_id) {
                r->players[glyph].animation=sprites->sequences[a];r->players[glyph].displayed=sprites->bindings[glyph].frame;
                r->players[glyph].cursor=0;r->players[glyph].revision=1;break;
            }
            if(!r->players[glyph].animation.count || dok_events_register(&r->scheduler,world->initial.objects[i].reference,4)!=DOK_STATUS_OK)
                return DOK_STATUS_INVALID_STATE;
        }
    }
    return DOK_STATUS_OK;
}
dok_status nds_effects_begin(NdsEffectsRuntime *r)
{
    dok_event_dispatch_report report;
    if(!r)return DOK_STATUS_INVALID_ARGUMENT;
    if(!EFFECT_REAL_CLOCK)r->fixture_ticks+=166u; /* Explicit deterministic source-time fixture, never a claim about render FPS. */
    if(dok_events_dispatch(&r->scheduler,&report)!=DOK_STATUS_OK || r->fault)return DOK_STATUS_INVALID_STATE;
    return DOK_STATUS_OK;
}
dok_status nds_effects_arm_bounds(NdsEffectsRuntime *r,const dok_render_gather_bounds *bounds,const dok_sprite_list *sprites)
{
    int32_t x,z;uint32_t i,width;
    unsigned char water_seen[256]={0};
    if(!r || !bounds || !sprites)return DOK_STATUS_INVALID_ARGUMENT;
    width=(uint32_t)r->world->effective.settings[DOK_LEVEL_WIDTH];
    /* The native builder calls floor getters throughout the gather AABB, before
     * projection rejection. Do not arm only the polygons surviving the frustum. */
    for(z=bounds->min_z;z<=bounds->max_z;++z)
        for(x=bounds->min_x;x<=bounds->max_x;++x) {
            uint32_t cell=(uint32_t)z*width+(uint32_t)x;
            uint32_t t=r->world->effective.glyph_index[r->world->initial.maps[DOK_WORLD_TMAP][cell]];
            if(!t || r->world->effective.objects[t-1u].class_id==2u)continue;
            uint32_t glyph=r->world->initial.maps[DOK_WORLD_FCMAP][cell];uint32_t index=r->world->initial.glyph_index[glyph],arm;
            if(!index || r->world->effective.objects[index-1u].class_id!=12u)continue;
            /* No callback dispatch occurs inside this builder sweep. First
             * getter resets18 to0 even on a capacity refusal; subsequent
             * getters of that glyph cannot rearm. Preserve first-call order. */
            if(water_seen[glyph])continue;
            water_seen[glyph]=1;
            if(dok_water_rearm(&r->water[glyph],&arm)!=DOK_STATUS_OK)return DOK_STATUS_INVALID_STATE;
            if(arm) {
                dok_status result=dok_events_schedule(&r->scheduler,r->world->initial.objects[index-1u].reference,13,glyph,0);
                /* Native getter resets counter first and ignores the native
                 * capacity refusal. Existing callbacks remain in the queue. */
                if(result==DOK_STATUS_NO_SPACE)++r->schedule_refusals;
                else if(result!=DOK_STATUS_OK)return result;
            }
        }
    for(i=0;i<sprites->count;++i) {
        uint32_t glyph=sprites->poses[i].glyph;
        if(glyph>255u || !r->players[glyph].animation.count || r->players[glyph].remaining)continue;
        uint32_t index=r->world->initial.glyph_index[glyph];
        r->players[glyph].remaining=200;
        if(!index || dok_events_schedule(&r->scheduler,r->world->initial.objects[index-1u].reference,12,glyph,
            (uint32_t)(int32_t)r->players[glyph].displayed.interval)!=DOK_STATUS_OK)return DOK_STATUS_INVALID_STATE;
    }
    return DOK_STATUS_OK;
}
