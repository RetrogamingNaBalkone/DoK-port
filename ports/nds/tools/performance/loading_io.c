/* Host audit: logical reader traffic, not DS timings or physical sector reads. */
#include "dok/frame_store.h"
#include "dok/render_materials.h"
#include "../../platform/assets/file_reader.h"
#include <stdio.h>
#include <string.h>
static dok_render_world world;
static dok_render_workspace work;
static dok_asset_workspace io;
static dok_sprite_asset_bank bank;
static dok_frame_store store;
static unsigned char assets[DOK_NDS_ASSETS_BYTES],pool[524288],scratch[131072];
static dok_asset_reader underlying;
static unsigned calls,bytes,catalog_calls,catalog_bytes,data_offset;
static dok_asset_status counted(void *context,uint32_t offset,unsigned char *dst,uint32_t n,uint32_t *actual)
{
    (void)context; ++calls;bytes+=n;
    if(offset>=160u&&offset<data_offset){++catalog_calls;catalog_bytes+=n;}
    return underlying.read_at(underlying.context,offset,dst,n,actual);
}
static void stage(unsigned level,const char *name)
{
    printf("{\"level\":%u,\"stage\":\"%s\",\"calls\":%u,\"bytes\":%u,\"catalog_calls\":%u,\"catalog_bytes\":%u,\"images\":%u,\"animations\":%u,\"decoded_bytes\":%u,\"packed_bytes\":%u}\n",
        level,name,calls,bytes,catalog_calls,catalog_bytes,bank.image_count,bank.animation_count,store.decoded_bytes,store.bytes);
    calls=bytes=catalog_calls=catalog_bytes=0;
}
int main(int argc,char **argv)
{
    NdsAssetFile file;dok_asset_session session;unsigned char header[160];uint32_t actual;
    if(argc!=2||nds_asset_file_open(&file,argv[1],&underlying)!=DOK_ASSET_OK)return 1;
    if(underlying.read_at(underlying.context,0,header,160,&actual)!=DOK_ASSET_OK||actual!=160)return 2;
    data_offset=header[24]|(uint32_t)header[25]<<8|(uint32_t)header[26]<<16|(uint32_t)header[27]<<24;
    dok_asset_reader reader={NULL,counted,underlying.file_bytes};
    if(dok_assets_open(&reader,&io,&session)!=DOK_ASSET_OK)return 3;
    stage(0,"open");
    for(unsigned level=1;level<=3;++level){
        memset(&bank,0,sizeof(bank));memset(&store,0,sizeof(store));
        if(dok_render_world_load(&session,level,level,&world,&work,assets,sizeof(assets))!=DOK_STATUS_OK)return 4;
        stage(level,"world");
        uint32_t off=(world.asset_bytes+31u)&~31u;dok_asset_view view;
        if(dok_assets_load(&session,(2u<<30)|((17u+level)<<18)|(DOK_ASSET_LEVEL_CP866<<10),assets+off,DOK_RENDER_SOURCE_BYTES,&view)!=DOK_ASSET_OK||
           dok_level_parse(&view,&work.world.level,&world.base)!=DOK_WORLD_OK||
           dok_presentation_bind(&world.effective,&world.base,&session,&work.presentation,&world.presentation)!=DOK_BALANCE_OK||
           dok_presentation_bind(&world.effective,&world.effective,&session,&work.presentation,&world.presentation)!=DOK_BALANCE_OK)return 5;
        stage(level,"localization_and_rebind");
        off=(off+view.entry.bytes+31u)&~31u;
        if(dok_sprite_assets_load(&world.presentation,&bank,assets+off,sizeof(assets)-off)!=DOK_STATUS_OK)return 6;
        stage(level,"sprite_metadata");
        if(dok_frame_store_load(&store,&bank,&session,pool,sizeof(pool),scratch,sizeof(scratch))!=DOK_STATUS_OK)return 7;
        stage(level,"frame_store");
    }
    nds_asset_file_close(&file);return 0;
}
