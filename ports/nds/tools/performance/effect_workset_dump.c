#include "dok/effect_workset.h"
#include "../../platform/assets/file_reader.h"
#include <inttypes.h>
#include <stdio.h>
static dok_render_world world;
static dok_render_workspace scratch;
static dok_asset_workspace io;
static unsigned char assets[DOK_NDS_ASSETS_BYTES];
static dok_effect_workset_record rows[DOK_RENDER_MATERIALS];
int main(int argc,char **argv)
{
    NdsAssetFile file;dok_asset_reader reader;dok_asset_session session;
    if(argc!=2||nds_asset_file_open(&file,argv[1],&reader)!=DOK_ASSET_OK||dok_assets_open(&reader,&io,&session)!=DOK_ASSET_OK)return 1;
    printf("{\"levels\":[");
    for(uint32_t level=1;level<=3u;++level){uint32_t bound,count;
        if(dok_render_world_load(&session,level,level,&world,&scratch,assets,sizeof(assets))!=DOK_STATUS_OK)return 2;
        if(dok_effect_workset_bound(&world,&bound,rows,&count)!=DOK_STATUS_OK)return 3;
        printf("%s{\"level\":%" PRIu32 ",\"bytes\":%" PRIu32 ",\"water_materials\":[",level>1u?",":"",level,bound);
        for(uint32_t i=0;i<count;++i)printf("%s{\"asset_id\":%" PRIu64 ",\"material\":%" PRIu32 ",\"flags\":%" PRIu32 ",\"code\":%" PRIu32 ",\"bytes\":%" PRIu32 "}",i?",":"",rows[i].asset_id,rows[i].material,rows[i].flags,rows[i].code,rows[i].bytes);
        printf("]}");
    }
    printf("]}\n");nds_asset_file_close(&file);return 0;
}
