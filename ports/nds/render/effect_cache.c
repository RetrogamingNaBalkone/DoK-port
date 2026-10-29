#include "dok/effect_cache.h"
#include <limits.h>
#include <string.h>
void dok_effect_cache_init(dok_effect_cache *c,unsigned char *pool,uint32_t capacity)
{ if(c) {memset(c,0,sizeof(*c));c->pool=pool;c->capacity=capacity;} }
dok_status dok_effect_cache_begin(dok_effect_cache *c)
{
    uint32_t i;
    if(!c || !c->pool || !c->capacity || c->age==UINT32_MAX)return DOK_STATUS_INVALID_ARGUMENT;
    for(i=0;i<c->count;++i)c->records[i].pinned=0;
    ++c->age;c->hits=c->misses=c->evictions=c->generated_bytes=0;return DOK_STATUS_OK;
}
dok_status dok_effect_cache_require(dok_effect_cache *c,uint64_t key,const unsigned char *source,
    uint16_t width,uint16_t height,uint16_t sw,uint16_t sh,uint16_t flags,uint16_t phase,
    const unsigned char *light_row,uint32_t masked)
{
    uint32_t bytes=(uint32_t)sw*sh,i;
    if(!c || !c->pool || !c->age || !key || !bytes || bytes>c->capacity)return DOK_STATUS_INVALID_ARGUMENT;
    for(i=0;i<c->count;++i)if(c->records[i].key==key) {
        if(c->records[i].bytes!=bytes)return DOK_STATUS_INVALID_ARGUMENT;
        c->records[i].pinned=1;c->records[i].age=c->age;++c->hits;return DOK_STATUS_OK;
    }
    /* Equal-sized animated water variants can replace an unpinned payload
     * directly. Moving the entire384KiB slab to free one16KiB image stalls
     * otherwise cheap GPU-darkened frames. Residency and LRU stay bounded. */
    if(bytes>c->capacity-c->used || c->count==DOK_EFFECT_CACHE_SLOTS) {
        uint32_t oldest=UINT32_MAX;
        for(i=0;i<c->count;++i)if(!c->records[i].pinned && c->records[i].bytes==bytes &&
            (oldest==UINT32_MAX || c->records[i].age<c->records[oldest].age))oldest=i;
        if(oldest!=UINT32_MAX) {
            uint32_t offset=c->records[oldest].offset;
            dok_status status=dok_effects_pixels(source,width,height,sw,sh,flags,18,phase,
                light_row,masked,c->pool+offset,bytes);
            if(status!=DOK_STATUS_OK)return status;
            c->records[oldest]=(dok_effect_cache_record){key,offset,bytes,c->age,1};
            ++c->evictions;++c->misses;c->generated_bytes+=bytes;return DOK_STATUS_OK;
        }
    }
    while(bytes>c->capacity-c->used || c->count==DOK_EFFECT_CACHE_SLOTS) {
        uint32_t oldest=UINT32_MAX;
        for(i=0;i<c->count;++i)if(!c->records[i].pinned &&
            (oldest==UINT32_MAX || c->records[i].age<c->records[oldest].age))oldest=i;
        if(oldest==UINT32_MAX)return DOK_STATUS_NO_SPACE;
        uint32_t offset=c->records[oldest].offset,size=c->records[oldest].bytes;
        memmove(c->pool+offset,c->pool+offset+size,c->used-offset-size);
        for(i=0;i<c->count;++i)if(c->records[i].offset>offset)c->records[i].offset-=size;
        c->records[oldest]=c->records[--c->count];c->used-=size;++c->evictions;
    }
    dok_status status=dok_effects_pixels(source,width,height,sw,sh,flags,18,phase,light_row,masked,
        c->pool+c->used,c->capacity-c->used);
    if(status!=DOK_STATUS_OK)return status;
    c->records[c->count++]=(dok_effect_cache_record){key,c->used,bytes,c->age,1};
    c->used+=bytes;++c->misses;c->generated_bytes+=bytes;return DOK_STATUS_OK;
}
const unsigned char *dok_effect_cache_pixels(const dok_effect_cache *c,uint64_t key)
{
    uint32_t i;if(!c || c->count>DOK_EFFECT_CACHE_SLOTS)return NULL;
    for(i=0;i<c->count;++i)if(c->records[i].key==key && c->records[i].pinned)
        return c->pool+c->records[i].offset;
    return NULL;
}
