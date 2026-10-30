#include "dok/frame_store.h"
#include "dok/overlay_reuse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned char images[8][256],packed[4096],pools[2][512],reference[49152],retained[49152];
static dok_asset_entry entries[8];static dok_frame_store store;static dok_texture_cache cache[2];
static uint32_t checks,evicted_retained,cold_reloads,hits;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main(void)
{
    store.pool=packed;
    for(uint32_t id=0;id<8u;++id){uint32_t bytes;
        for(uint32_t i=0;i<256u;++i)images[id][i]=(unsigned char)(i*13u+id*17u);
        CHECK(dok_frame_pack(images[id],256,packed+store.bytes,sizeof(packed)-store.bytes,&bytes)==DOK_STATUS_OK);
        store.records[store.count++]=(dok_frame_record){id+1u,store.bytes,bytes,256};store.bytes+=bytes;
        entries[id]=(dok_asset_entry){.id=id+1u,.bytes=256,.width=16,.height=16,.storage_width=16,.storage_height=16};
    }
    for(uint32_t i=0;i<2u;++i)dok_texture_cache_init(&cache[i],pools[i],sizeof(pools[i]));
    memset(reference,0xa5,sizeof(reference));memset(retained,0xa5,sizeof(retained));
    dok_overlay_key key={.view={.frame={.image=1,.width=16,.height=16},.visible=1},.serial=1,.bytes=256,.pitch=16},previous={0};uint32_t valid=0;
    for(uint32_t frame=0;frame<512u;++frame){
        uint32_t world_id=frame%7u+1u;
        if(frame%31u==0u){++key.view.left;}
        if(frame%97u==0u){valid=0;memset(retained,77,256u*154u);}
        uint32_t reuse=valid&&dok_overlay_key_equal(&key,&previous);
        for(uint32_t mode=0;mode<2u;++mode){
            CHECK(dok_texture_cache_begin(&cache[mode])==DOK_STATUS_OK);
            CHECK(dok_frame_store_require(&store,&cache[mode],&entries[world_id])==DOK_STATUS_OK);
            if(!mode||!reuse){
                uint32_t before=store.decodes;
                CHECK(dok_frame_store_require(&store,&cache[mode],&entries[0])==DOK_STATUS_OK);
                if(mode&&store.decodes!=before)++cold_reloads;
                if(mode){CHECK(dok_weapon_overlay_clipped(&key.view,dok_texture_cache_pixels(&cache[mode],1),16,256,retained,sizeof(retained))==DOK_STATUS_OK);previous=key;valid=1;}
            }else{++hits;if(!dok_texture_cache_pixels(&cache[mode],1))++evicted_retained;}
            const unsigned char *world_pixels=dok_texture_cache_pixels(&cache[mode],world_id+1u);
            CHECK(world_pixels&&!memcmp(world_pixels,images[world_id],256));
        }
        CHECK(dok_weapon_overlay(&key.view,images[0],16,256,reference,sizeof(reference))==DOK_STATUS_OK);
        CHECK(!memcmp(reference,retained,sizeof(reference)));
    }
    CHECK(evicted_retained>100u&&cold_reloads>10u&&hits>400u);
    printf("{\"passed\":true,\"frames\":512,\"checks\":%u,\"retained_hits\":%u,\"weapon_absent_while_retained\":%u,\"cold_reload_on_changed_view\":%u,\"compared_bytes\":25165824}\n",checks,hits,evicted_retained,cold_reloads);
    return 0;
}
