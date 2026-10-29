#include "dok/texture_cache.h"
#include <string.h>
#include <limits.h>
void dok_texture_cache_init(dok_texture_cache *c,unsigned char *pool,uint32_t capacity)
{
    if(c) { memset(c,0,sizeof(*c));c->pool=pool;c->capacity=capacity; }
}
dok_status dok_texture_cache_begin(dok_texture_cache *c)
{
    uint32_t i;
    if(!c || !c->pool || !c->capacity || c->age==UINT32_MAX) return DOK_STATUS_INVALID_ARGUMENT;
    for(i=0;i<c->count;++i)c->records[i].pinned=0;
    ++c->age;c->hits=c->misses=c->evictions=c->read_bytes=0;return DOK_STATUS_OK;
}
static void evict(dok_texture_cache *c,uint32_t index)
{
    uint32_t i,offset=c->records[index].offset,bytes=c->records[index].bytes;
    /* Every record size is a power of two >=64 (accepted GPU index8). */
    memmove(c->pool+offset,c->pool+offset+bytes,c->used-offset-bytes);
    for(i=0;i<c->count;++i)if(c->records[i].offset>offset)c->records[i].offset-=bytes;
    c->records[index]=c->records[--c->count];c->used-=bytes;++c->evictions;
}
dok_status dok_texture_cache_require(dok_texture_cache *c,const dok_asset_session *s,const dok_asset_entry *e)
{
    uint32_t i;dok_asset_view view;
    if(!c || !s || !e || !c->pool || !c->age || e->representation!=DOK_ASSET_GPU_INDEX8_POT ||
       e->bytes<64u || (e->bytes&(e->bytes-1u)) || e->bytes>c->capacity)
        return DOK_STATUS_INVALID_ARGUMENT;
    for(i=0;i<c->count;++i)if(c->records[i].key==e->id) {
        if(c->records[i].bytes!=e->bytes)return DOK_STATUS_INVALID_ARGUMENT;
        c->records[i].pinned=1;c->records[i].age=c->age;++c->hits;return DOK_STATUS_OK;
    }
    while(e->bytes>c->capacity-c->used || c->count==DOK_TEXTURE_CACHE_SLOTS) {
        uint32_t oldest=UINT32_MAX;
        for(i=0;i<c->count;++i)if(!c->records[i].pinned &&
            (oldest==UINT32_MAX || c->records[i].age<c->records[oldest].age))oldest=i;
        if(oldest==UINT32_MAX)return DOK_STATUS_NO_SPACE;
        evict(c,oldest);
    }
    if(dok_assets_load(s,e->id,c->pool+c->used,c->capacity-c->used,&view)!=DOK_ASSET_OK)
        return DOK_STATUS_INVALID_STATE;
    if(view.entry.bytes!=e->bytes || view.entry.representation!=e->representation ||
       view.entry.storage_width!=e->storage_width || view.entry.storage_height!=e->storage_height)
        return DOK_STATUS_INVALID_STATE;
    c->records[c->count++]=(dok_texture_cache_record){e->id,c->used,e->bytes,c->age,1};
    c->used+=e->bytes;++c->misses;c->read_bytes+=e->bytes;return DOK_STATUS_OK;
}
const unsigned char *dok_texture_cache_pixels(const dok_texture_cache *c,dok_asset_id id)
{
    uint32_t i;if(!c || c->count>DOK_TEXTURE_CACHE_SLOTS)return NULL;
    for(i=0;i<c->count;++i)if(c->records[i].key==id && c->records[i].pinned)
        return c->pool+c->records[i].offset;
    return NULL;
}
