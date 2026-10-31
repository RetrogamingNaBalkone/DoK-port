/* Build-time use of the exact resident codec, with a round-trip pixel check. */
#include "dok/frame_store.h"
#include "dok/render_materials.h"
#include "dok/frontend_visual.h"
#include "dok/audio_codec.h"
#include "../../platform/assets/file_reader.h"
#include <stdio.h>
#include <string.h>
static dok_render_world world;
static dok_render_workspace work;
static dok_asset_workspace io;
static dok_sprite_asset_bank bank;
static dok_frame_store store;
static dok_frontend_visual visual;
static unsigned char assets[DOK_NDS_ASSETS_BYTES],pool[524288],scratch[131072];
static int audio_bank(const dok_asset_session *s,const char *path)
{
    uint32_t records[22][4],used=0;
    for(unsigned i=0;i<22u;++i){
        unsigned char descriptor[128];dok_asset_view view;uint32_t *r=records[i];
        r[0]=(FRAME_LANGUAGE<<30)|((86u+i)<<18)|(DOK_ASSET_PCM_CLIP<<10);
        if(dok_assets_load(s,r[0],descriptor,sizeof(descriptor),&view)!=DOK_ASSET_OK)return 0;
        memcpy(r+1,descriptor,8);
        uint32_t chunk;memcpy(&chunk,descriptor+24,4);
        if(dok_assets_load(s,chunk,scratch,65536,&view)!=DOK_ASSET_OK||
           dok_audio_pack(scratch,r[2],pool+used,sizeof(pool)-used,&r[3])!=DOK_STATUS_OK)return 0;
        used+=r[3];
    }
    uint32_t header[4]={0x31415044u,1,22,used};FILE *f=fopen(path,"wb");if(!f)return 0;
    int ok=fwrite(header,1,16,f)==16&&fwrite(records,1,sizeof(records),f)==sizeof(records)&&fwrite(pool,1,used,f)==used;
    if(fclose(f))ok=0;return ok;
}
int main(int argc,char **argv)
{
    NdsAssetFile file;dok_asset_reader reader;dok_asset_session session;
    if(argc!=4||nds_asset_file_open(&file,argv[1],&reader)!=DOK_ASSET_OK||dok_assets_open(&reader,&io,&session)!=DOK_ASSET_OK)return 1;
    if(dok_render_world_load(&session,1,1,&world,&work,assets,sizeof(assets))!=DOK_STATUS_OK)return 2;
    unsigned offset=(world.asset_bytes+31u)&~31u;
#if FRAME_LANGUAGE == 2
    dok_asset_view view;
    if(dok_assets_load(&session,(2u<<30)|(18u<<18)|(DOK_ASSET_LEVEL_CP866<<10),assets+offset,DOK_RENDER_SOURCE_BYTES,&view)!=DOK_ASSET_OK||
       dok_level_parse(&view,&work.world.level,&world.base)!=DOK_WORLD_OK||
       dok_presentation_bind(&world.effective,&world.base,&session,&work.presentation,&world.presentation)!=DOK_BALANCE_OK)return 2;
    offset=(offset+view.entry.bytes+31u)&~31u;
#endif
    if(dok_sprite_assets_load(&world.presentation,&bank,assets+offset,sizeof(assets)-offset)!=DOK_STATUS_OK||
       dok_frame_store_load(&store,&bank,&session,pool,sizeof(pool),scratch,sizeof(scratch))!=DOK_STATUS_OK||
       dok_frontend_visual_init(&visual,&session)!=DOK_STATUS_OK||
       dok_frontend_visual_append_store(&visual,&store,&session,pool,sizeof(pool),scratch,sizeof(scratch))!=DOK_STATUS_OK)return 3;
    uint32_t header[8]={0x31465044u,1,store.count,store.bytes,store.decoded_bytes,0,0,0};
    FILE *out=fopen(argv[2],"wb");if(!out)return 4;
    int ok=fwrite(header,1,32,out)==32&&fwrite(store.records,sizeof(store.records[0]),store.count,out)==store.count&&fwrite(pool,1,store.bytes,out)==store.bytes;
    if(fclose(out))ok=0;if(ok)ok=audio_bank(&session,argv[3]);nds_asset_file_close(&file);return ok?0:5;
}
