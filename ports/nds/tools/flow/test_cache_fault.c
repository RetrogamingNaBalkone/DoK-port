/* Actual frame_store_require against read-only ordinary-run snapshot data. */
#include "dok/frame_store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)){fprintf(stderr,"check %u line %u\n",checks,__LINE__);exit(1);} } while(0)
/* Resident require does not call asset I/O. Link-only stub rejects accidental use. */
dok_asset_status dok_assets_load(const dok_asset_session *s,dok_asset_id id,
    unsigned char *candidate,uint32_t capacity,dok_asset_view *out)
{(void)s;(void)id;(void)candidate;(void)capacity;(void)out;CHECK(0);return DOK_ASSET_INVALID_STATE;}

static void read_exact(FILE *file,void *out,size_t bytes)
{CHECK(fread(out,1,bytes,file)==bytes);}
static uint32_t pinned(const dok_texture_cache *cache)
{uint32_t total=0;for(uint32_t i=0;i<cache->count;++i){CHECK(cache->records[i].pinned==1);total+=cache->records[i].bytes;}return total;}

int main(int argc,char **argv)
{
    uint32_t header[16];dok_texture_cache captured={0};dok_frame_store store={0};
    dok_asset_entry requests[32];
    CHECK(argc==3);FILE *input=fopen(argv[1],"rb");CHECK(input);
    read_exact(input,header,sizeof(header));CHECK(header[0]==0x4343324e);
    uint32_t capacity=header[1],used=header[2],age=header[3],count=header[4],
        request_count=header[5],frame_count=header[6],packed_bytes=header[7],failed_index=header[8];
    CHECK(count<=256 && request_count<=32 && frame_count<=256 && failed_index<request_count);
    CHECK(sizeof(dok_texture_cache_record)==24 && sizeof(dok_asset_entry)==120 && sizeof(dok_frame_record)==16);
    captured.capacity=capacity;captured.used=used;captured.age=age;captured.count=count;
    captured.hits=header[9];captured.misses=header[10];captured.evictions=header[11];captured.read_bytes=header[12];
    read_exact(input,captured.records,count*sizeof(captured.records[0]));
    unsigned char *old_pixels=malloc(used);CHECK(old_pixels);read_exact(input,old_pixels,used);
    store.count=frame_count;store.bytes=packed_bytes;
    store.decoded_bytes=header[13];store.decodes=header[14];store.decoded_total=header[15];
    read_exact(input,store.records,frame_count*sizeof(store.records[0]));
    unsigned char *packed=malloc(packed_bytes);CHECK(packed);read_exact(input,packed,packed_bytes);store.pool=packed;
    read_exact(input,requests,request_count*sizeof(requests[0]));CHECK(fgetc(input)==EOF);fclose(input);

    /* Exact failure prefix: all observed current-frame records are pinned. */
    captured.pool=malloc(capacity);CHECK(captured.pool);memset(captured.pool,0xa5,capacity);memcpy(captured.pool,old_pixels,used);
    CHECK(pinned(&captured)==used);
    dok_texture_cache before=captured;dok_frame_store frames_before=store;
    unsigned char *before_pixels=malloc(capacity);CHECK(before_pixels);memcpy(before_pixels,captured.pool,capacity);
    dok_status result=dok_frame_store_require(&store,&captured,&requests[failed_index]);
    CHECK(result==DOK_STATUS_NO_SPACE);CHECK(!memcmp(&before,&captured,sizeof(before)));
    CHECK(!memcmp(&frames_before,&store,sizeof(store)));CHECK(!memcmp(before_pixels,captured.pool,capacity));
    free(before_pixels);free(captured.pool);

    const uint32_t capacities[]={221056,229375,229376,237567,237568};
    uint32_t failures[5]={0},successes=0;
    for(uint32_t trial=0;trial<5;++trial){
        dok_texture_cache cache;unsigned char *pixels=malloc(capacities[trial]);CHECK(pixels);
        dok_texture_cache_init(&cache,pixels,capacities[trial]);CHECK(dok_texture_cache_begin(&cache)==DOK_STATUS_OK);
        uint32_t i;
        for(i=0;i<request_count;++i){
            CHECK(requests[i].representation==DOK_ASSET_GPU_INDEX8_POT);
            result=dok_frame_store_require(&store,&cache,&requests[i]);
            if(result!=DOK_STATUS_OK){CHECK(result==DOK_STATUS_NO_SPACE);break;}
            CHECK(dok_texture_cache_pixels(&cache,requests[i].id)!=NULL);
        }
        failures[trial]=i;CHECK(pinned(&cache)==cache.used);
        if(trial<2){CHECK(i==failed_index && cache.used==196608);}
        else if(trial<4){CHECK(i==request_count-1 && cache.used==229376);}
        else {
            CHECK(i==request_count && cache.used==237568);++successes;
            FILE *output=fopen(argv[2],"wb");CHECK(output);
            for(i=0;i<request_count;++i){
                const unsigned char *p=dok_texture_cache_pixels(&cache,requests[i].id);CHECK(p);
                CHECK(fwrite(&requests[i].id,4,1,output)==1);CHECK(fwrite(&requests[i].bytes,4,1,output)==1);
                CHECK(fwrite(p,1,requests[i].bytes,output)==requests[i].bytes);
            }
            CHECK(fclose(output)==0);
        }
        free(pixels);
    }
    printf("{\"checks\":%u,\"captured_no_space\":true,\"failure_indices\":[%u,%u,%u,%u,%u],\"successful_caps\":%u,\"requests\":%u,\"minimum_current_bytes\":237568}\n",
        checks,failures[0],failures[1],failures[2],failures[3],failures[4],successes,request_count);
    free(old_pixels);free(packed);return 0;
}
