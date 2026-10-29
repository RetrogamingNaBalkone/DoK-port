#include "dok/frame_store.h"
#include <string.h>
#include <limits.h>
dok_status dok_frame_pack(const unsigned char *in,uint32_t n,unsigned char *out,uint32_t cap,uint32_t *used)
{
    uint32_t i=0,p=0;if(!in||!out||!used)return DOK_STATUS_INVALID_ARGUMENT;
    while(i<n){uint32_t run=1;
        while(run<128u&&i+run<n&&in[i+run]==in[i])++run;
        if(run>=3u){if(cap-p<2u)return DOK_STATUS_NO_SPACE;out[p++]=(unsigned char)(128u+run-1u);out[p++]=in[i];i+=run;}
        else {uint32_t start=i;i+=run;
            while(i<n&&i-start<128u){run=1;while(run<3u&&i+run<n&&in[i+run]==in[i])++run;if(run>=3u)break;i+=run<128u-(i-start)?run:128u-(i-start);}
            uint32_t count=i-start;if(cap-p<1u+count)return DOK_STATUS_NO_SPACE;out[p++]=(unsigned char)(count-1u);memcpy(out+p,in+start,count);p+=count;
        }
    }
    *used=p;return DOK_STATUS_OK;
}
dok_status dok_frame_unpack(const unsigned char *in,uint32_t n,unsigned char *out,uint32_t bytes)
{
    uint32_t i=0,p=0;if(!in||!out)return DOK_STATUS_INVALID_ARGUMENT;
    while(i<n){uint32_t code=in[i++],count=(code&127u)+1u;if(count>bytes-p)return DOK_STATUS_INVALID_STATE;
        if(code&128u){if(i==n)return DOK_STATUS_INVALID_STATE;memset(out+p,in[i++],count);}
        else {if(count>n-i)return DOK_STATUS_INVALID_STATE;memcpy(out+p,in+i,count);i+=count;}p+=count;
    }
    return p==bytes?DOK_STATUS_OK:DOK_STATUS_INVALID_STATE;
}
dok_status dok_frame_store_load(dok_frame_store *s,const dok_sprite_asset_bank *b,const dok_asset_session *session,unsigned char *pool,uint32_t capacity,unsigned char *scratch,uint32_t scratch_bytes)
{
    if(!s||!b||!session||!pool||!scratch)return DOK_STATUS_INVALID_ARGUMENT;
    memset(s,0,sizeof(*s));s->pool=pool;
    for(uint32_t i=0;i<b->image_count;++i){const dok_asset_entry *e=&b->images[i];dok_asset_view view;uint32_t used;
        if(s->count==DOK_TEXTURE_CACHE_SLOTS||e->bytes>scratch_bytes/2u||s->bytes>capacity)return DOK_STATUS_NO_SPACE;
        if(dok_assets_load(session,e->id,scratch,scratch_bytes/2u,&view)!=DOK_ASSET_OK)return DOK_STATUS_INVALID_STATE;
        dok_status status=dok_frame_pack(scratch,e->bytes,pool+s->bytes,capacity-s->bytes,&used);if(status!=DOK_STATUS_OK)return status;
        /* Validate the actual resident representation once, before gameplay. */
        if(dok_frame_unpack(pool+s->bytes,used,scratch+scratch_bytes/2u,e->bytes)!=DOK_STATUS_OK || memcmp(scratch,scratch+scratch_bytes/2u,e->bytes))return DOK_STATUS_INVALID_STATE;
        s->records[s->count++]=(dok_frame_record){e->id,s->bytes,used,e->bytes};s->bytes+=used;s->decoded_bytes+=e->bytes;
    }
    return DOK_STATUS_OK;
}
static void evict(dok_texture_cache *c,uint32_t i)
{
    uint32_t offset=c->records[i].offset,bytes=c->records[i].bytes;
    memmove(c->pool+offset,c->pool+offset+bytes,c->used-offset-bytes);
    for(uint32_t j=0;j<c->count;++j)if(c->records[j].offset>offset)c->records[j].offset-=bytes;
    c->records[i]=c->records[--c->count];c->used-=bytes;++c->evictions;
}
dok_status dok_frame_store_require(dok_frame_store *s,dok_texture_cache *c,const dok_asset_entry *e)
{
    if(!s||!c||!e||!c->pool||!c->age||s->count>DOK_TEXTURE_CACHE_SLOTS||c->count>DOK_TEXTURE_CACHE_SLOTS||c->used>c->capacity||e->bytes>c->capacity)return DOK_STATUS_INVALID_ARGUMENT;
    for(uint32_t i=0;i<c->count;++i)if(c->records[i].key==e->id){if(c->records[i].bytes!=e->bytes)return DOK_STATUS_INVALID_STATE;c->records[i].pinned=1;c->records[i].age=c->age;++c->hits;return DOK_STATUS_OK;}
    const dok_frame_record *r=NULL;for(uint32_t i=0;i<s->count;++i)if(s->records[i].id==e->id){r=&s->records[i];break;}
    if(!r||r->decoded_bytes!=e->bytes||r->offset>s->bytes||r->bytes>s->bytes-r->offset)return DOK_STATUS_INVALID_STATE;
    while(e->bytes>c->capacity-c->used||c->count==DOK_TEXTURE_CACHE_SLOTS){uint32_t oldest=UINT32_MAX;
        for(uint32_t i=0;i<c->count;++i)if(!c->records[i].pinned&&(oldest==UINT32_MAX||c->records[i].age<c->records[oldest].age))oldest=i;
        if(oldest==UINT32_MAX)return DOK_STATUS_NO_SPACE;evict(c,oldest);
    }
    dok_status result=dok_frame_unpack(s->pool+r->offset,r->bytes,c->pool+c->used,e->bytes);if(result!=DOK_STATUS_OK)return result;
    c->records[c->count++]=(dok_texture_cache_record){e->id,c->used,e->bytes,c->age,1};c->used+=e->bytes;++c->misses;++s->decodes;s->decoded_total+=e->bytes;
    return DOK_STATUS_OK;
}
