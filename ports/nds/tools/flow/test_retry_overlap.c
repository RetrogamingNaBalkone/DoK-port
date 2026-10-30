#include "gameplay.h"
#include "effects_runtime.h"
#include "file_reader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static NdsGameplay game;
static NdsEffectsRuntime runtime;
static dok_render_world world;
static dok_render_workspace scratch;
static dok_asset_workspace io;
static dok_sprite_asset_bank sprites;
static dok_event_bank events;
static dok_initial_world saved_world;
static unsigned char assets[DOK_NDS_ASSETS_BYTES];
static dok_asset_session session;
static uint32_t ticks,callback_status,callback_count,checks,total_ai_callbacks,repeated_retries,corpse_variants;
static uint32_t indices[3],origin=4u*64u+5u,death_capture,death_mmap,death_tmap;
static uint8_t glyphs[3],hero_glyph;
#if defined(ARM9)
extern void nds_overlap_fixture_fail(uint32_t line);
#define NEED(e) do{++checks;if(!(e))nds_overlap_fixture_fail(__LINE__);}while(0)
void nds_overlap_fixture_metrics(uint32_t out[16])
{
    const uint32_t measured[16]={checks,callback_count,callback_status,game.fault,game.ai.fault,
        game.ai.callbacks,total_ai_callbacks,game.retries,repeated_retries,corpse_variants,
        sizeof(game.collision),sizeof(game.collision._respawn_origins),game.collision._respawn_cell,
        sizeof(game),sizeof(world),sizeof(assets)};
    memcpy(out,measured,sizeof(measured));
}
#else
#define NEED(e) do{++checks;if(!(e)){fprintf(stderr,"retry overlap line%d: %s\n",__LINE__,#e);exit(2);}}while(0)
#endif
static dok_status now(void *p,uint32_t *out){(void)p;*out=ticks;return DOK_STATUS_OK;}
static dok_enemy *enemy(unsigned i){return &game.ai.enemies[game.ai.glyph_index[glyphs[i]]-1u];}
static dok_object_ref ref(unsigned i){return world.initial.objects[indices[i]].reference;}
static void callback(void *p,dok_event_scheduler *s,dok_object_ref owner,uint16_t kind,uint32_t parameter)
{
    (void)p;(void)s;++callback_count;
    /* Keep the hero's event live, but isolate actual AI callbacks so collision
     * checks observe temporary MMap overwrites before another hero update. */
    if(kind!=DOK_AI_CALLBACK)return;
    dok_status result=nds_gameplay_callback(&game,&runtime,owner,kind,parameter);
    ++total_ai_callbacks;if(result!=DOK_STATUS_OK)callback_status=result;
    if(owner.object_id==0x30000u+glyphs[0]&&enemy(0)->corpse){
        death_capture=1;death_mmap=world.initial.maps[DOK_WORLD_MMAP][origin];
        death_tmap=world.initial.maps[DOK_WORLD_TMAP][origin];
    }
}
dok_status nds_effects_arm_bounds(NdsEffectsRuntime *r,const dok_render_gather_bounds *b,const dok_sprite_list *s)
{(void)r;(void)b;(void)s;abort();}
static void sweep(uint32_t advance)
{
    ticks+=advance;dok_event_dispatch_report report;
    NEED(dok_events_dispatch(&runtime.scheduler,&report)==DOK_STATUS_OK);
    NEED(!callback_status&&!game.fault&&!game.ai.fault);
}
static void setup(void)
{
    ticks=100000;callback_status=callback_count=0;death_capture=0;
    memset(&runtime,0,sizeof(runtime));
    NEED(dok_render_world_load(&session,1,1,&world,&scratch,assets,sizeof(assets))==DOK_STATUS_OK);
    uint32_t offset=(world.asset_bytes+31u)&~31u;
    NEED(dok_sprite_assets_load(&world.presentation,&sprites,assets+offset,sizeof(assets)-offset)==DOK_STATUS_OK);
    memset(world.initial.maps,' ',3u*DOK_LEVEL_MAX_CELLS);
    dok_world_object_seed *hero=&world.initial.objects[world.initial.hero.object_id-1u];
    hero->x=2640;hero->z=6000;hero->heading=180;
    unsigned found=0;
    for(uint32_t i=0;i<world.initial.object_count&&found<3u;++i)if(world.initial.objects[i].class_id==9u){
        indices[found]=i;glyphs[found]=world.effective.objects[i].glyph;
        dok_world_object_seed *seed=&world.initial.objects[i];
        seed->x=2640;seed->z=found<2u?2160:10080;seed->heading=0;seed->pose_known=1;++found;
    }
    NEED(found==3u);hero_glyph=world.effective.objects[hero->definition_index].glyph;
    world.initial.maps[DOK_WORLD_MMAP][origin]=glyphs[1];
    world.initial.maps[DOK_WORLD_MMAP][12u*64u+5u]=hero_glyph;
    runtime.world=&world;runtime.sprites=&sprites;
    dok_event_config config={0,now,0,callback,world.initial.epoch};
    NEED(dok_events_init(&runtime.scheduler,&events,&config)==DOK_STATUS_OK);
    NEED(nds_gameplay_bind(&game,&runtime)==DOK_STATUS_OK);
    for(unsigned i=0;i<2u;++i){uint32_t accepted=0;
        NEED(dok_ai_hit(&game.ai,ref(i),hero->reference,1,&accepted)==DOK_STATUS_OK&&accepted);
        enemy(i)->motion.speed=0;
        enemy(i)->course_counter=(int16_t)dok_level_field_at(&world.effective.objects[indices[i]],0xae)->value;
    }
    game.spawn.x=2640;game.spawn.y=-300;game.spawn.z=2160;game.spawn.heading=180;
}
static void retry(void)
{
    game.items.inventory.health=0;
    NEED(dok_gameflow_check(0,&game.lives)==DOK_FLOW_RETRY);
    NEED(nds_gameplay_retry(&game)==DOK_STATUS_OK);
    NEED(world.initial.maps[DOK_WORLD_MMAP][origin]==hero_glyph);
    NEED(dok_collision_respawn_origin(&game.collision,world.initial.hero,origin));
    NEED(dok_collision_respawn_origin(&game.collision,ref(0),origin));
    NEED(dok_collision_respawn_origin(&game.collision,ref(1),origin));
    NEED(!dok_collision_respawn_origin(&game.collision,ref(2),origin));
}
static void movement_guards(void)
{
    dok_collision_context context;dok_collision_report report;dok_collision_plan plan;uint32_t clear=0;
    NEED(game.ai.callbacks>=2u&&game.ai.course_trials>0u);
    NEED(dok_collision_respawn_origin(&game.collision,world.initial.hero,origin));
    NEED(dok_collision_respawn_origin(&game.collision,ref(0),origin));
    NEED(dok_collision_respawn_origin(&game.collision,ref(1),origin));
    saved_world=world.initial;
    NEED(dok_ai_sight(&game.ai,ref(0),&clear)==DOK_STATUS_OK);
    NEED(memcmp(&world.initial,&saved_world,sizeof(saved_world))==0);
    NEED(dok_collision_camera_clear(&game.collision,world.initial.hero,game.hero.x,game.hero.z,game.hero.x,game.hero.z,&clear)==DOK_STATUS_OK&&clear);
    NEED(dok_collision_begin(&game.collision,world.initial.hero,&game.hero,416,&context)==DOK_STATUS_OK);
    dok_motion_substep request={game.hero.x,game.hero.y,game.hero.z,game.hero.x,game.hero.z,0,0,0};
    NEED(dok_collision_query(&context,&request,&plan)==DOK_STATUS_OK);
    NEED(dok_collision_commit(&context,&plan)==DOK_STATUS_OK);
    NEED(dok_collision_end(&context,&report)==DOK_STATUS_OK);
    NEED(dok_collision_respawn_origin(&game.collision,world.initial.hero,origin));
    /* Rejected and accepted same-cell moves cannot retire the permission. */
    NEED(dok_collision_begin(&game.collision,world.initial.hero,&game.hero,416,&context)==DOK_STATUS_OK);
    request.candidate_x=game.hero.x+240;request.distance=240;
    NEED(dok_collision_query(&context,&request,&plan)==DOK_STATUS_OK);
    NEED(plan._reason==DOK_COLLISION_LARGE_STEP);
    NEED(dok_collision_commit(&context,&plan)==DOK_STATUS_OK);
    NEED(dok_collision_end(&context,&report)==DOK_STATUS_OK&&report.rejected);
    NEED(dok_collision_respawn_origin(&game.collision,world.initial.hero,origin));
    NEED(dok_collision_displace(&game.collision,world.initial.hero,&game.hero,game.hero.x+1,game.hero.z,&report)==DOK_STATUS_OK);
    NEED(dok_collision_respawn_origin(&game.collision,world.initial.hero,origin));
    dok_motion_state foreign=enemy(2)->motion;foreign.x=2640;foreign.z=2160;
    NEED(dok_collision_begin(&game.collision,ref(2),&foreign,416,&context)==DOK_STATUS_INVALID_STATE);
    NEED(dok_collision_camera_clear(&game.collision,ref(2),foreign.x,foreign.z,foreign.x,foreign.z,&clear)==DOK_STATUS_INVALID_STATE);
    NEED(dok_collision_displace(&game.collision,ref(2),&foreign,foreign.x+1,foreign.z,&report)==DOK_STATUS_INVALID_STATE);
    dok_object_ref stale=ref(0);++stale.generation;
    NEED(!dok_collision_respawn_origin(&game.collision,stale,origin));
    NEED(dok_collision_begin(&game.collision,stale,&enemy(0)->motion,416,&context)==DOK_STATUS_STALE_REFERENCE);
    NEED(dok_collision_camera_clear(&game.collision,stale,2640,2160,2640,2160,&clear)==DOK_STATUS_STALE_REFERENCE);
    NEED(dok_collision_displace(&game.collision,stale,&foreign,2641,2160,&report)==DOK_STATUS_STALE_REFERENCE);
    stale=ref(0);++stale.world_epoch;
    NEED(!dok_collision_respawn_origin(&game.collision,stale,origin));
    NEED(dok_collision_begin(&game.collision,stale,&enemy(0)->motion,416,&context)==DOK_STATUS_STALE_REFERENCE);
    NEED(dok_collision_camera_clear(&game.collision,stale,2640,2160,2640,2160,&clear)==DOK_STATUS_STALE_REFERENCE);
    NEED(dok_collision_displace(&game.collision,stale,&foreign,2641,2160,&report)==DOK_STATUS_STALE_REFERENCE);
    uint32_t other=origin+64;world.initial.maps[DOK_WORLD_MMAP][other]=glyphs[2];
    foreign=enemy(0)->motion;foreign.z+=480;
    NEED(!dok_collision_respawn_origin(&game.collision,ref(0),other));
    NEED(dok_collision_begin(&game.collision,ref(0),&foreign,416,&context)==DOK_STATUS_INVALID_STATE);
    NEED(dok_collision_camera_clear(&game.collision,ref(0),foreign.x,foreign.z,foreign.x,foreign.z,&clear)==DOK_STATUS_INVALID_STATE);
    NEED(dok_collision_displace(&game.collision,ref(0),&foreign,foreign.x+1,foreign.z,&report)==DOK_STATUS_INVALID_STATE);
    world.initial.maps[DOK_WORLD_MMAP][other]=' ';
    /* Committed departure clears only the departing actor's identity. */
    NEED(dok_collision_displace(&game.collision,world.initial.hero,&game.hero,3120,2160,&report)==DOK_STATUS_OK);
    NEED(game.hero.x==3120&&game.hero.z==2160);
    NEED(!dok_collision_respawn_origin(&game.collision,world.initial.hero,origin));
    NEED(dok_collision_respawn_origin(&game.collision,ref(0),origin));
    NEED(dok_collision_respawn_origin(&game.collision,ref(1),origin));
    foreign=game.hero;foreign.x=2640;world.initial.maps[DOK_WORLD_MMAP][origin]=glyphs[0];
    NEED(dok_collision_begin(&game.collision,world.initial.hero,&foreign,416,&context)==DOK_STATUS_INVALID_STATE);
    NEED(dok_collision_displace(&game.collision,world.initial.hero,&foreign,2641,2160,&report)==DOK_STATUS_INVALID_STATE);
    /* Re-entry may be rejected by the occupied destination. Neither that
     * rejection nor a later ordinary re-entry through space rearms the bit. */
    NEED(dok_collision_displace(&game.collision,world.initial.hero,&game.hero,2640,2160,&report)==DOK_STATUS_OK);
    NEED(game.hero.x/480==6&&!dok_collision_respawn_origin(&game.collision,world.initial.hero,origin));
    world.initial.maps[DOK_WORLD_MMAP][origin]=' ';
    NEED(dok_collision_displace(&game.collision,world.initial.hero,&game.hero,2640,2160,&report)==DOK_STATUS_OK);
    NEED(game.hero.x/480==5&&!dok_collision_respawn_origin(&game.collision,world.initial.hero,origin));
    world.initial.maps[DOK_WORLD_MMAP][origin]=glyphs[0];
    NEED(dok_collision_begin(&game.collision,world.initial.hero,&game.hero,416,&context)==DOK_STATUS_INVALID_STATE);
    NEED(dok_collision_displace(&game.collision,ref(0),&enemy(0)->motion,2160,2160,&report)==DOK_STATUS_OK);
    NEED(!dok_collision_respawn_origin(&game.collision,ref(0),origin));
    NEED(dok_collision_respawn_origin(&game.collision,ref(1),origin));
    ++world.initial.epoch;
    NEED(!dok_collision_respawn_origin(&game.collision,ref(0),origin));
    NEED(dok_collision_begin(&game.collision,ref(0),&enemy(0)->motion,416,&context)==DOK_STATUS_INVALID_STATE);
    NEED(dok_collision_camera_clear(&game.collision,ref(0),2640,2160,2640,2160,&clear)==DOK_STATUS_INVALID_STATE);
    NEED(dok_collision_displace(&game.collision,ref(0),&foreign,2641,2160,&report)==DOK_STATUS_INVALID_STATE);
    --world.initial.epoch;
}
static void corpse_transfer(unsigned occupied_tile)
{
    setup();retry();
    uint8_t tile=' ';
    if(occupied_tile){
        for(uint32_t i=0;i<world.initial.object_count;++i)if(world.effective.objects[i].class_id==3u){tile=world.effective.objects[i].glyph;break;}
        NEED(tile!=' ');world.initial.maps[DOK_WORLD_TMAP][origin]=tile;
    }
    uint32_t accepted=0;
    NEED(dok_ai_hit(&game.ai,ref(0),world.initial.hero,32767,&accepted)==DOK_STATUS_OK&&accepted);
    for(unsigned step=0;step<64u&&!death_capture;++step)sweep(5000);
    NEED(death_capture&&enemy(0)->corpse);
    NEED(game.ai.corpses[glyphs[0]/8u]&(1u<<(glyphs[0]%8u)));
    NEED(death_mmap==(occupied_tile?glyphs[0]:' '));
    NEED(death_tmap==(occupied_tile?tile:glyphs[0]));
    NEED(game.collision.flags[glyphs[0]]&16u);
    ++corpse_variants;
}
int main(int argc,char **argv)
{
    NEED(argc==2);NdsAssetFile file;dok_asset_reader reader;
    NEED(nds_asset_file_open(&file,argv[1],&reader)==DOK_ASSET_OK);
    NEED(dok_assets_open(&reader,&io,&session)==DOK_ASSET_OK);
    setup();retry();sweep(416);retry();sweep(3000);movement_guards();
    repeated_retries=game.retries;NEED(repeated_retries==2u);
    corpse_transfer(0);corpse_transfer(1);
#if !defined(ARM9)
    printf("{\"reproduced\":false,\"checks\":%u,\"retry_count\":%u,\"callback_count\":%u,\"callback_status\":%u,\"game_fault\":%u,\"ai_fault\":%u,\"enemy_glyph\":%u,\"origin_after\":%u,\"ai_callbacks\":%u,\"total_ai_callbacks\":%u,\"corpse_variants\":2,\"shared_actors\":3,\"repeated_retry_count\":%u}\n",
       checks,game.retries,callback_count,callback_status,game.fault,game.ai.fault,glyphs[0],
       world.initial.maps[DOK_WORLD_MMAP][origin],game.ai.callbacks,total_ai_callbacks,repeated_retries);
#endif
    return 0;
}
