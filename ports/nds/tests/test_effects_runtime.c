#include "../platform/effects_probe/effects_runtime.h"
#include "../platform/assets/file_reader.h"
#include "dok/sha256.h"
#include <stdio.h>
#include <string.h>
static dok_render_world world;
static dok_sprite_asset_bank bank;
static dok_sprite_list sprites;
static dok_render_workspace scratch;
static dok_render_scene scene;
static dok_asset_workspace io;
static dok_event_bank events;
static NdsEffectsRuntime runtime;
static unsigned char assets[DOK_NDS_ASSETS_BYTES];
static uint64_t counter(void *context){(void)context;return 0;}
static void hash(unsigned char out[32]){dok_sha256 s;dok_sha256_begin(&s);dok_sha256_update(&s,&world.initial,sizeof(world.initial));dok_sha256_end(&s,out);}
#define CHECK(c) do{++checks;if(!(c)){fprintf(stderr,"effects runtime:%d level%u step%u\n",__LINE__,level,step);return 1;}}while(0)
int main(int argc,char **argv)
{
    NdsAssetFile file;dok_asset_reader reader;dok_asset_session session;
    dok_platform_services platform={0};unsigned checks=0,level=0,step=0;unsigned char initial[32],actual[32];
    if(argc!=2)return 2;
    platform.read_counter=counter;platform.counter_hz_numerator=10000;platform.counter_hz_denominator=1;
    CHECK(nds_asset_file_open(&file,argv[1],&reader)==DOK_ASSET_OK);
    CHECK(dok_assets_open(&reader,&io,&session)==DOK_ASSET_OK);
    for(level=1;level<=3;++level) {
        CHECK(dok_render_world_load(&session,level,level,&world,&scratch,assets,sizeof(assets))==DOK_STATUS_OK);
        uint32_t offset=(world.asset_bytes+31u)&~31u;
        CHECK(dok_sprite_assets_load(&world.presentation,&bank,assets+offset,sizeof(assets)-offset-DOK_EFFECT_CACHE_BYTES)==DOK_STATUS_OK);
        runtime.trace_total=0;
        CHECK(nds_effects_load(&runtime,&world,&bank,&session,&reader,&events,&platform,
            assets+sizeof(assets)-DOK_EFFECT_CACHE_BYTES,DOK_EFFECT_CACHE_BYTES)==DOK_STATUS_OK);
        hash(initial);dok_render_scene_init(&scene);
        for(step=0;step<500;++step) {
            dok_nds_render_camera camera={level==1?15120:level==2?2160:17520,-300,level==1?22800:level==2?2160:28560,0,0};
            const dok_nds_render_frame *frame;const dok_sprite_list *list;
            CHECK(nds_effects_begin(&runtime)==DOK_STATUS_OK);
            CHECK(dok_render_trig((step*3u)%360u,&camera.sin_q15,&camera.cos_q15));
            CHECK(dok_render_scene_prepare(&scene,&world.initial,world.bindings,&camera,(step*3u)%360u,&frame)==DOK_RENDER_SCENE_OK);
            dok_sprite_snapshot snapshot={world.initial.epoch,scene.generation,bank.bindings,&world.initial};
            CHECK(dok_sprites_gather(&snapshot,&camera,(step*3u)%360u,&sprites,&list)==DOK_STATUS_OK);
            CHECK(nds_effects_arm(&runtime,&scene,&sprites)==DOK_STATUS_OK);
            CHECK(dok_render_scene_release(&scene,frame->generation)==DOK_RENDER_SCENE_OK);
            hash(actual);CHECK(!memcmp(initial,actual,32));
        }
        CHECK(!runtime.fault);
        if(level==2){CHECK(runtime.water_callbacks>500u);CHECK(runtime.schedule_refusals>0);}
    }
    nds_asset_file_close(&file);printf("{\"checks\":%u,\"failures\":0,\"service_sweeps\":1500}\n",checks);return 0;
}
