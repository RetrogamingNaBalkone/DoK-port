#include "collision_routes.h"
#include "dok/render_materials.h"
#include "file_reader.h"
#include <stdio.h>
#include <string.h>
static dok_render_world render;
static dok_render_workspace load_workspace;
static dok_collision_routes_workspace replay;
static dok_event_bank bank;
static dok_collision_state collision,before_collision;
static dok_initial_world before_world;
static dok_level_definition localized;
static dok_level_presentation en_presentation;
static unsigned char assets[DOK_NDS_ASSETS_BYTES],en_bytes[65536];
static dok_asset_workspace catalog;
static dok_asset_session session;
static dok_collision_routes_report baseline,result;
static uint32_t checks,failures;
#define CHECK(value) do { ++checks;if(!(value)) {++failures;fprintf(stderr,"N10 route driver line%d\n",__LINE__);return 1;} } while(0)
int main(int argc,char **argv)
{
    NdsAssetFile file;dok_asset_reader reader;uint32_t level,language;
    CHECK(argc==2);
    CHECK(nds_asset_file_open(&file,argv[1],&reader)==DOK_ASSET_OK);
    CHECK(dok_assets_open(&reader,&catalog,&session)==DOK_ASSET_OK);
    printf("{\"levels\":[");
    for(level=1u;level<=3u;++level) {
        dok_asset_view view;
        dok_asset_id en_id=UINT32_C(2)<<30 | (17u+level)<<18 | (uint32_t)DOK_ASSET_LEVEL_CP866<<10;
        CHECK(dok_render_world_load(&session,level,level,&render,&load_workspace,assets,sizeof(assets))==DOK_STATUS_OK);
        memset(&collision,0,sizeof(collision));
        CHECK(dok_collision_bind(&collision,&render.initial)==DOK_STATUS_OK);
        before_world=render.initial;before_collision=collision;
        CHECK(dok_assets_load(&session,en_id,en_bytes,sizeof(en_bytes),&view)==DOK_ASSET_OK);
        CHECK(dok_level_parse(&view,&load_workspace.world.level,&localized)==DOK_WORLD_OK);
        CHECK(dok_presentation_bind(&render.effective,&localized,&session,
                                   &load_workspace.presentation,&en_presentation)==DOK_BALANCE_OK);
        CHECK(en_presentation.language==2u && render.presentation.language==1u);
        for(language=1u;language<=2u;++language) {
            if(language==2u) render.presentation=en_presentation;
            if(dok_collision_routes_run(&result,&render.initial,&collision,&bank,&replay)!=0) {
                fprintf(stderr,"route level%lu line%lu failures%lu errors%lu requested%lu adjusted%lu rejected%lu use%lu fire%lu\n",
                        (unsigned long)level,(unsigned long)result.first_failed_line,(unsigned long)result.failures,
                        (unsigned long)replay.context.errors,(unsigned long)result.requested,
                        (unsigned long)result.adjusted,(unsigned long)result.rejected,
                        (unsigned long)result.use_pulses,(unsigned long)result.short_fire_pulses);return 1;
            }
            CHECK(result.failures==0u && result.checks!=0u && result.runs==3u);
            CHECK(memcmp(&before_world,&render.initial,sizeof(before_world))==0);
            CHECK(memcmp(&before_collision,&collision,sizeof(before_collision))==0);
            if(language==1u) baseline=result;
            else {
                CHECK(result.trace_hash==baseline.trace_hash && result.final_hash==baseline.final_hash);
                CHECK(result.map_hash==baseline.map_hash && result.counter_hash==baseline.counter_hash);
                CHECK(memcmp(&result.final_motion,&baseline.final_motion,sizeof(result.final_motion))==0);
                CHECK(result.requested==baseline.requested && result.committed==baseline.committed &&
                      result.adjusted==baseline.adjusted && result.rejected==baseline.rejected);
            }
            checks+=result.checks;failures+=result.failures;
            printf("%s{\"level\":%lu,\"language\":%lu,\"checks\":%lu,\"failures\":%lu,"
                   "\"first_failed_line\":%lu,\"callbacks\":%lu,\"trace_hash\":%lu,\"final_hash\":%lu,"
                   "\"map_hash\":%lu,\"counter_hash\":%lu,\"requested\":%lu,\"committed\":%lu,"
                   "\"adjusted\":%lu,\"rejected\":%lu,\"observations_30\":%lu,\"observations_60\":%lu,"
                   "\"x\":%ld,\"y\":%ld,\"z\":%ld,\"heading\":%d}",
                   level==1u&&language==1u?"":",",(unsigned long)level,(unsigned long)language,
                   (unsigned long)result.checks,(unsigned long)result.failures,(unsigned long)result.first_failed_line,
                   (unsigned long)result.callbacks,(unsigned long)result.trace_hash,(unsigned long)result.final_hash,
                   (unsigned long)result.map_hash,(unsigned long)result.counter_hash,
                   (unsigned long)result.requested,(unsigned long)result.committed,
                   (unsigned long)result.adjusted,(unsigned long)result.rejected,
                   (unsigned long)result.observations_30,(unsigned long)result.observations_60,
                   (long)result.final_motion.x,(long)result.final_motion.y,(long)result.final_motion.z,
                   (int)result.final_motion.heading);
        }
    }
    nds_asset_file_close(&file);
    printf("],\"checks\":%lu,\"failures\":%lu,\"workspace_bytes\":%lu}\n",
           (unsigned long)checks,(unsigned long)failures,(unsigned long)sizeof(replay));
    return failures!=0u;
}
