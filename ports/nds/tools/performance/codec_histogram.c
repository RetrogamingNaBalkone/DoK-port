#include "dok/frame_store.h"
#include "dok/render_materials.h"
#include "../../platform/assets/file_reader.h"
#include <inttypes.h>
#include <stdio.h>
static dok_render_world world;static dok_render_workspace workspace;static dok_asset_workspace io;
static dok_sprite_asset_bank bank;static dok_frame_store frames;
static unsigned char assets[DOK_NDS_ASSETS_BYTES],packed[524288],scratch[131072];
int main(int argc,char **argv)
{
    NdsAssetFile file;dok_asset_reader reader;dok_asset_session session;uint32_t literals[129]={0},runs[129]={0},offset;
    if(argc!=2||nds_asset_file_open(&file,argv[1],&reader)!=DOK_ASSET_OK||dok_assets_open(&reader,&io,&session)!=DOK_ASSET_OK)return 1;
    if(dok_render_world_load(&session,1,1,&world,&workspace,assets,sizeof(assets))!=DOK_STATUS_OK)return 2;
    offset=(world.asset_bytes+31u)&~31u;
    if(dok_sprite_assets_load(&world.presentation,&bank,assets+offset,sizeof(assets)-offset)!=DOK_STATUS_OK||
       dok_frame_store_load(&frames,&bank,&session,packed,sizeof(packed),scratch,sizeof(scratch))!=DOK_STATUS_OK)return 3;
    for(uint32_t f=0;f<frames.count;++f){const dok_frame_record *r=&frames.records[f];uint32_t p=r->offset,end=p+r->bytes;
        while(p<end){uint32_t code=packed[p++],count=(code&127u)+1u;if(code&128u){++runs[count];++p;}else{++literals[count];p+=count;}}}
    printf("{\"frames\":%" PRIu32 ",\"packed_bytes\":%" PRIu32 ",\"decoded_bytes\":%" PRIu32 ",\"literal_blocks\":[",frames.count,frames.bytes,frames.decoded_bytes);
    for(uint32_t i=0;i<=128u;++i)printf("%s%" PRIu32,i?",":"",literals[i]);printf("],\"run_blocks\":[");
    for(uint32_t i=0;i<=128u;++i)printf("%s%" PRIu32,i?",":"",runs[i]);printf("]}\n");nds_asset_file_close(&file);return 0;
}
