#include "dok/frame_store.h"
#include "dok/water_texture.h"
#include "dok/render_materials.h"
#include "../platform/assets/file_reader.h"
#include <stdio.h>
#include <string.h>
static dok_render_world world;static dok_render_workspace work;static dok_asset_workspace io;
static dok_sprite_asset_bank bank;static dok_frame_store store;static dok_texture_cache cache;
static unsigned char assets[DOK_NDS_ASSETS_BYTES],pool[524288],raw[65536],packed[131072],decoded[65536],water[32768],reference[16384];
static uint32_t checks;
#define CHECK(e) do{++checks;if(!(e)){fprintf(stderr,"frame_store:%d\n",__LINE__);return 1;}}while(0)
int main(int argc,char **argv)
{
    uint32_t used,seed=171029u;
    for(uint32_t n=0;n<=65536u;n=n<256u?n+1u:n+257u){
        for(uint32_t i=0;i<n;++i){seed=seed*1664525u+1013904223u;raw[i]=(unsigned char)((i/128u)%3u?seed>>24u:i/128u);}
        CHECK(dok_frame_pack(raw,n,packed,sizeof(packed),&used)==DOK_STATUS_OK);
        CHECK(dok_frame_unpack(packed,used,decoded,n)==DOK_STATUS_OK);CHECK(!memcmp(raw,decoded,n));
        if(n)CHECK(dok_frame_unpack(packed,used,decoded,n-1u)!=DOK_STATUS_OK);
    }
    CHECK(dok_frame_unpack((const unsigned char *)"\377",1,decoded,128)!=DOK_STATUS_OK);
    CHECK(dok_frame_unpack((const unsigned char *)"\177x",2,decoded,128)!=DOK_STATUS_OK);
    NdsAssetFile file;dok_asset_reader reader;dok_asset_session session;
    CHECK(argc==2);CHECK(nds_asset_file_open(&file,argv[1],&reader)==DOK_ASSET_OK);CHECK(dok_assets_open(&reader,&io,&session)==DOK_ASSET_OK);
    CHECK(dok_render_world_load(&session,1,1,&world,&work,assets,sizeof(assets))==DOK_STATUS_OK);
    uint32_t off=(world.asset_bytes+31u)&~31u;
    CHECK(dok_sprite_assets_load(&world.presentation,&bank,assets+off,sizeof(assets)-off)==DOK_STATUS_OK);
    CHECK(dok_frame_store_load(&store,&bank,&session,pool,300000,packed,sizeof(packed))==DOK_STATUS_OK);
    uint32_t reads=file.calls;
    dok_texture_cache_init(&cache,pool+300000,sizeof(pool)-300000);
    for(uint32_t pass=0;pass<3u;++pass)for(uint32_t i=0;i<bank.image_count;++i){
        CHECK(dok_texture_cache_begin(&cache)==DOK_STATUS_OK);CHECK(dok_frame_store_require(&store,&cache,&bank.images[i])==DOK_STATUS_OK);
        CHECK(file.calls==reads);dok_asset_view view;CHECK(dok_assets_load(&session,bank.images[i].id,raw,sizeof(raw),&view)==DOK_ASSET_OK);reads=file.calls;
        CHECK(!memcmp(raw,dok_texture_cache_pixels(&cache,bank.images[i].id),view.entry.bytes));
    }
    uint32_t water_cases=0;
    for(uint32_t i=0;i<dok_assets_count(&session);++i){dok_asset_entry e;CHECK(dok_assets_entry_at(&session,i,&e)==DOK_ASSET_OK);
        if(e.representation!=DOK_ASSET_GPU_INDEX8_POT||e.width!=96u||e.height!=96u)continue;
        dok_asset_view view;CHECK(dok_assets_load(&session,e.id,raw,sizeof(raw),&view)==DOK_ASSET_OK);
        for(uint32_t axis=0;axis<2u;++axis){CHECK(dok_water_texture(raw,96,96,128,axis,water,sizeof(water))==DOK_STATUS_OK);
            for(uint32_t direction=0;direction<2u;++direction)for(uint32_t phase=0;phase<18u;++phase){
                uint16_t flags=(uint16_t)(axis?(direction?0x2000:0x1000):(direction?0x0800:0x0400));
                CHECK(dok_effects_pixels(raw,96,96,128,128,flags,18,(uint16_t)phase,NULL,0,reference,sizeof(reference))==DOK_STATUS_OK);
                uint32_t shift=dok_water_uv_shift(phase,96,!direction),pitch=axis?128u:256u;
                for(uint32_t y=0;y<96u;++y)for(uint32_t x=0;x<96u;++x)CHECK(reference[y*128u+x]==water[(y+(axis?shift:0u))*pitch+x+(axis?0u:shift)]);
                ++water_cases;
            }
        }
    }
    nds_asset_file_close(&file);printf("{\"checks\":%lu,\"failures\":0,\"frames\":%lu,\"packed_bytes\":%lu,\"decoded_bytes\":%lu,\"water_pairs\":%lu}\n",(unsigned long)checks,(unsigned long)store.count,(unsigned long)store.bytes,(unsigned long)store.decoded_bytes,(unsigned long)water_cases);return 0;
}
