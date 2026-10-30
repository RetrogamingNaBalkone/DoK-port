#include "dok/frontend_visual.h"
#include <limits.h>
#include <string.h>

#define VISUAL_TAG UINT32_C(0x31565044)
#define POPAL_KEY ((145u<<18)|(5u<<10)|5u)
#define LANGUAGE_MASK UINT32_C(0x3fffffff)
#define FRAME_BYTES 32768u

static int shape(const dok_asset_entry *e,unsigned language)
{
    return e->id==((language<<30)|POPAL_KEY)&&e->language==language&&
        e->source_id==((language<<30)|(145u<<18)|(1u<<10))&&
        e->representation==DOK_ASSET_GPU_INDEX8_POT&&e->source_kind==2u&&
        e->source_index==145u&&e->subindex==5u&&e->width==158u&&
        e->height==70u&&e->storage_width==256u&&e->storage_height==128u&&
        e->bytes==FRAME_BYTES&&e->param1==8u;
}
static int same(const dok_asset_entry *a,const dok_asset_entry *b)
{
    return shape(b,a->language)&&a->offset==b->offset&&a->param0==b->param0&&
        !memcmp(a->sha256,b->sha256,32)&&
        !memcmp(a->source_sha256,b->source_sha256,32);
}
dok_status dok_frontend_visual_init(dok_frontend_visual *out,
                                    const dok_asset_session *session)
{
    dok_frontend_visual value;
    if(!out||!session)return DOK_STATUS_INVALID_ARGUMENT;
    memset(&value,0,sizeof(value));
    for(unsigned i=0;i<2u;++i){
        if(dok_assets_find(session,((i+1u)<<30)|POPAL_KEY,&value.entries[i])!=DOK_ASSET_OK||
           !shape(&value.entries[i],i+1u))return DOK_STATUS_INVALID_STATE;
    }
    value.tag=VISUAL_TAG;*out=value;return DOK_STATUS_OK;
}
const dok_asset_entry *dok_frontend_visual_resolve(const dok_frontend_visual *v,
    const dok_asset_entry *source,uint8_t language)
{
    if(!v||v->tag!=VISUAL_TAG||!source||language<1u||language>2u||
       (source->id&LANGUAGE_MASK)!=POPAL_KEY)return source;
    return &v->entries[language-1u];
}
static int overlap(const unsigned char *a,uint32_t an,
                   const unsigned char *b,uint32_t bn)
{
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x<=y?y-x<an:x-y<bn;
}
dok_status dok_frontend_visual_append_store(const dok_frontend_visual *v,
    dok_frame_store *s,const dok_asset_session *session,unsigned char *pool,
    uint32_t capacity,unsigned char *scratch,uint32_t scratch_bytes)
{
    if(!v||!s||!session||!pool||!scratch||v->tag!=VISUAL_TAG||
       s->pool!=pool||s->count>DOK_TEXTURE_CACHE_SLOTS||s->bytes>capacity||
       overlap(pool,capacity,scratch,scratch_bytes))return DOK_STATUS_INVALID_ARGUMENT;
    if(scratch_bytes/2u<FRAME_BYTES)return DOK_STATUS_NO_SPACE;
    uint32_t decoded=0,found[2]={0,0},missing=0;
    for(uint32_t i=0;i<s->count;++i){
        const dok_frame_record *r=&s->records[i];
        if(!r->bytes||!r->decoded_bytes||r->offset>s->bytes||r->bytes>s->bytes-r->offset||
           r->decoded_bytes>UINT32_MAX-decoded)return DOK_STATUS_INVALID_STATE;
        decoded+=r->decoded_bytes;
        for(uint32_t j=0;j<i;++j)if(s->records[j].id==r->id)return DOK_STATUS_INVALID_STATE;
        for(unsigned j=0;j<2u;++j)if(r->id==v->entries[j].id){
            if(r->decoded_bytes!=FRAME_BYTES)return DOK_STATUS_INVALID_STATE;
            found[j]=1;
        }
    }
    if(decoded!=s->decoded_bytes)return DOK_STATUS_INVALID_STATE;
    for(unsigned i=0;i<2u;++i){
        dok_asset_entry current;
        if(!shape(&v->entries[i],i+1u)||
           dok_assets_find(session,v->entries[i].id,&current)!=DOK_ASSET_OK||
           !same(&v->entries[i],&current))return DOK_STATUS_INVALID_STATE;
        if(!found[i])++missing;
    }
    if(missing>DOK_TEXTURE_CACHE_SLOTS-s->count||
       missing>(UINT32_MAX-s->decoded_bytes)/FRAME_BYTES)return DOK_STATUS_NO_SPACE;
    dok_frame_record appended[2];uint32_t count=0,bytes=0;
    for(unsigned i=0;i<2u;++i)if(!found[i]){
        dok_asset_view view;uint32_t used;
        if(dok_assets_load(session,v->entries[i].id,scratch,scratch_bytes/2u,&view)!=DOK_ASSET_OK||
           !same(&v->entries[i],&view.entry))return DOK_STATUS_INVALID_STATE;
        dok_status status=dok_frame_pack(scratch,FRAME_BYTES,pool+s->bytes+bytes,
                                         capacity-s->bytes-bytes,&used);
        if(status!=DOK_STATUS_OK)return status;
        if(dok_frame_unpack(pool+s->bytes+bytes,used,scratch+scratch_bytes/2u,FRAME_BYTES)!=DOK_STATUS_OK||
           memcmp(scratch,scratch+scratch_bytes/2u,FRAME_BYTES))return DOK_STATUS_INVALID_STATE;
        appended[count++]=(dok_frame_record){v->entries[i].id,s->bytes+bytes,used,FRAME_BYTES};
        bytes+=used;
    }
    for(uint32_t i=0;i<count;++i)s->records[s->count+i]=appended[i];
    s->count+=count;s->bytes+=bytes;s->decoded_bytes+=count*FRAME_BYTES;
    return DOK_STATUS_OK;
}
