#include "dok/frame_store.h"
#include "dok/n23_profile.h"
#include <string.h>
#include <limits.h>
#ifndef N23_BATCH_EVICTION
#define N23_BATCH_EVICTION 1
#endif
#ifndef N23_CODEC_SMALL
#define N23_CODEC_SMALL 0
#endif
#ifndef N23_CACHE_REPLACE
#define N23_CACHE_REPLACE 0
#endif
#if N23_CACHE_REPLACE
static uint32_t packed_valid(const dok_frame_store *,const dok_frame_record *);
static uint32_t replacement_source_ok(const dok_frame_store *s,const dok_frame_record *r,const dok_texture_cache *c)
{
    if(!s->pool)return 0;
    /* Protect future resident packed records as well as this request. */
    uintptr_t source=(uintptr_t)s->pool,destination=(uintptr_t)c->pool;
    uintptr_t source_end=source+s->bytes,destination_end=destination+c->capacity;
    return source_end>=source&&destination_end>=destination&&
        (source_end<=destination||destination_end<=source)&&packed_valid(s,r);
}
#endif
#if N23_CODEC_SMALL
static void literal_copy(unsigned char *out,const unsigned char *in,uint32_t count)
{
#if N23_CODEC_SMALL == 2
#define COPY_CASE(n) case n: out[(n)-1u]=in[(n)-1u]; __attribute__((fallthrough));
    switch(count){
        COPY_CASE(16) COPY_CASE(15) COPY_CASE(14) COPY_CASE(13)
        COPY_CASE(12) COPY_CASE(11) COPY_CASE(10) COPY_CASE(9)
        COPY_CASE(8) COPY_CASE(7) COPY_CASE(6) COPY_CASE(5)
        COPY_CASE(4) COPY_CASE(3) COPY_CASE(2)
        case 1: out[0]=in[0];break;
        default:memcpy(out,in,count);break;
    }
#undef COPY_CASE
#else
    if(count<=4u){
        out[0]=in[0];
        if(count>=2u)out[1]=in[1];
        if(count>=3u)out[2]=in[2];
        if(count==4u)out[3]=in[3];
    }else memcpy(out,in,count);
#endif
}
static void run_fill(unsigned char *out,unsigned char value,uint32_t count)
{
#if N23_CODEC_SMALL == 2
#define FILL_CASE(n) case n: out[(n)-1u]=value; __attribute__((fallthrough));
    switch(count){
        FILL_CASE(16) FILL_CASE(15) FILL_CASE(14) FILL_CASE(13)
        FILL_CASE(12) FILL_CASE(11) FILL_CASE(10) FILL_CASE(9)
        FILL_CASE(8) FILL_CASE(7) FILL_CASE(6) FILL_CASE(5)
        FILL_CASE(4) FILL_CASE(3) FILL_CASE(2)
        case 1:out[0]=value;break;
        default:memset(out,value,count);break;
    }
#undef FILL_CASE
#else
    if(count<=4u){
        out[0]=value;
        if(count>=2u)out[1]=value;
        if(count>=3u)out[2]=value;
        if(count==4u)out[3]=value;
    }else memset(out,value,count);
#endif
}
#else
#define literal_copy(out,in,count) memcpy(out,in,count)
#define run_fill(out,value,count) memset(out,value,count)
#endif
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
        if(code&128u){if(i==n)return DOK_STATUS_INVALID_STATE;run_fill(out+p,in[i++],count);}
        else {if(count>n-i)return DOK_STATUS_INVALID_STATE;literal_copy(out+p,in+i,count);i+=count;}p+=count;
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
#if !N23_BATCH_EVICTION
static void evict(dok_texture_cache *c,uint32_t i)
{
    N23_TIME(start);
    uint32_t offset=c->records[i].offset,bytes=c->records[i].bytes;
    N23_ADD(4,c->used-offset-bytes);
    memmove(c->pool+offset,c->pool+offset+bytes,c->used-offset-bytes);
    for(uint32_t j=0;j<c->count;++j)if(c->records[j].offset>offset)c->records[j].offset-=bytes;
    c->records[i]=c->records[--c->count];c->used-=bytes;++c->evictions;
    N23_ADD(0,dok_n23_profile_time()-start);N23_ADD(5,1u);
}
#else
/* Keep the exact LRU selection and swap-removal order, including equal ages.
 * Offsets are updated logically at each removal; payload movement is deferred.
 * Sorted holes refer to the original packed pool. Each surviving span moves
 * at most once, rather than once for every preceding victim. */
static dok_status make_room(dok_texture_cache *c,uint32_t bytes)
{
    struct hole {uint32_t offset,bytes;} holes[DOK_TEXTURE_CACHE_SLOTS];
    uint32_t count=0,original_used=c->used;
    dok_status status=DOK_STATUS_OK;
    N23_TIME(start);
    while(bytes>c->capacity-c->used||c->count==DOK_TEXTURE_CACHE_SLOTS){
        uint32_t oldest=UINT32_MAX;
        for(uint32_t i=0;i<c->count;++i)
            if(!c->records[i].pinned&&(oldest==UINT32_MAX||c->records[i].age<c->records[oldest].age))oldest=i;
        if(oldest==UINT32_MAX){status=DOK_STATUS_NO_SPACE;break;}
        uint32_t offset=c->records[oldest].offset,size=c->records[oldest].bytes,physical=offset;
        for(uint32_t i=0;i<count&&holes[i].offset<=physical;++i)physical+=holes[i].bytes;
        uint32_t position=count;
        while(position&&holes[position-1u].offset>physical){holes[position]=holes[position-1u];--position;}
        holes[position]=(struct hole){physical,size};++count;
        for(uint32_t i=0;i<c->count;++i)if(c->records[i].offset>offset)c->records[i].offset-=size;
        c->records[oldest]=c->records[--c->count];c->used-=size;++c->evictions;
    }
    if(count){
        uint32_t read=holes[0].offset,write=read;
        for(uint32_t i=0;i<count;++i){
            uint32_t size=holes[i].offset-read;
            if(size){memmove(c->pool+write,c->pool+read,size);N23_ADD(4,size);write+=size;}
            read=holes[i].offset+holes[i].bytes;
        }
        if(read<original_used){uint32_t size=original_used-read;memmove(c->pool+write,c->pool+read,size);N23_ADD(4,size);}
    }
    N23_ADD(0,dok_n23_profile_time()-start);N23_ADD(5,count);
    return status;
}
#endif
dok_status dok_frame_store_require(dok_frame_store *s,dok_texture_cache *c,const dok_asset_entry *e)
{
    if(!s||!c||!e||!c->pool||!c->age||s->count>DOK_TEXTURE_CACHE_SLOTS||c->count>DOK_TEXTURE_CACHE_SLOTS||c->used>c->capacity||e->bytes>c->capacity)return DOK_STATUS_INVALID_ARGUMENT;
    for(uint32_t i=0;i<c->count;++i)if(c->records[i].key==e->id){if(c->records[i].bytes!=e->bytes)return DOK_STATUS_INVALID_STATE;c->records[i].pinned=1;c->records[i].age=c->age;++c->hits;return DOK_STATUS_OK;}
    const dok_frame_record *r=NULL;for(uint32_t i=0;i<s->count;++i)if(s->records[i].id==e->id){r=&s->records[i];break;}
    if(!r||r->decoded_bytes!=e->bytes||r->offset>s->bytes||r->bytes>s->bytes-r->offset)return DOK_STATUS_INVALID_STATE;
#if N23_CACHE_REPLACE
    /* Physical offsets are allocator state, not borrowed pointers. An exact
     * size replacement leaves every old live span in place and fills the
     * evicted hole; all requires still finish before any pointer is bound.
     * Prevalidate before mutation so malformed streams retain the old path. */
    if(e->bytes>c->capacity-c->used||c->count==DOK_TEXTURE_CACHE_SLOTS){
        uint32_t oldest=UINT32_MAX;
        for(uint32_t i=0;i<c->count;++i)
            if(!c->records[i].pinned&&(oldest==UINT32_MAX||c->records[i].age<c->records[oldest].age))oldest=i;
        if(oldest!=UINT32_MAX&&c->records[oldest].bytes==e->bytes&&replacement_source_ok(s,r,c)){
            uint32_t offset=c->records[oldest].offset,bytes=e->bytes;dok_asset_id id=e->id;
            c->records[oldest]=c->records[--c->count];c->used-=bytes;++c->evictions;N23_ADD(5,1u);
            N23_TIME(decode_start);
            dok_status result=dok_frame_unpack(s->pool+r->offset,r->bytes,c->pool+offset,bytes);
            N23_ADD(1,dok_n23_profile_time()-decode_start);N23_ADD(6,1u);
            if(result!=DOK_STATUS_OK)return result;
            c->records[c->count++]=(dok_texture_cache_record){id,offset,bytes,c->age,1};c->used+=bytes;
            ++c->misses;++s->decodes;s->decoded_total+=bytes;return DOK_STATUS_OK;
        }
    }
#endif
#if N23_BATCH_EVICTION
    if(e->bytes>c->capacity-c->used||c->count==DOK_TEXTURE_CACHE_SLOTS){
        dok_status status=make_room(c,e->bytes);if(status!=DOK_STATUS_OK)return status;
    }
#else
    while(e->bytes>c->capacity-c->used||c->count==DOK_TEXTURE_CACHE_SLOTS){uint32_t oldest=UINT32_MAX;
        for(uint32_t i=0;i<c->count;++i)if(!c->records[i].pinned&&(oldest==UINT32_MAX||c->records[i].age<c->records[oldest].age))oldest=i;
        if(oldest==UINT32_MAX)return DOK_STATUS_NO_SPACE;evict(c,oldest);
    }
#endif
    N23_TIME(decode_start);
    dok_status result=dok_frame_unpack(s->pool+r->offset,r->bytes,c->pool+c->used,e->bytes);
    N23_ADD(1,dok_n23_profile_time()-decode_start);N23_ADD(6,1u);
    if(result!=DOK_STATUS_OK)return result;
    c->records[c->count++]=(dok_texture_cache_record){e->id,c->used,e->bytes,c->age,1};c->used+=e->bytes;++c->misses;++s->decodes;s->decoded_total+=e->bytes;
    return DOK_STATUS_OK;
}

static dok_status require_sequence(dok_frame_store *s,dok_texture_cache *c,
    const dok_asset_entry *const *entries,uint32_t count)
{
    for(uint32_t i=0;i<count;++i){dok_status status=dok_frame_store_require(s,c,entries[i]);if(status!=DOK_STATUS_OK)return status;}
    return DOK_STATUS_OK;
}

static uint32_t packed_valid(const dok_frame_store *s,const dok_frame_record *r)
{
    uint32_t i=0,decoded=0;
    if(!s->pool||r->offset>s->bytes||r->bytes>s->bytes-r->offset)return 0;
    const unsigned char *input=s->pool+r->offset;
    while(i<r->bytes){uint32_t code=input[i++],count=(code&127u)+1u;
        if(count>r->decoded_bytes-decoded)return 0;
        if(code&128u){if(i==r->bytes)return 0;++i;}
        else {if(count>r->bytes-i)return 0;i+=count;}
        decoded+=count;
    }
    return decoded==r->decoded_bytes;
}

dok_status dok_frame_store_require_many(dok_frame_store *s,dok_texture_cache *c,
    const dok_asset_entry *const *entries,uint32_t count,dok_frame_batch_workspace *w)
{
    if(!s||!c||!w||(!entries&&count)||count>DOK_TEXTURE_CACHE_SLOTS)return DOK_STATUS_INVALID_ARGUMENT;
    /* Warm hit prefixes touch no workspace and do exactly the ordinary lookup.
     * Decode without pressure also needs no shadow or compaction planning. */
    uint32_t first=0;
    for(;first<count;++first){const dok_asset_entry *e=entries[first];
        if(!e||!c->pool||!c->age||s->count>DOK_TEXTURE_CACHE_SLOTS||c->count>DOK_TEXTURE_CACHE_SLOTS||c->used>c->capacity||e->bytes>c->capacity)
            return require_sequence(s,c,entries+first,count-first);
        uint32_t i;for(i=0;i<c->count;++i)if(c->records[i].key==e->id)break;
        if(i<c->count){
            if(c->records[i].bytes!=e->bytes)return DOK_STATUS_INVALID_STATE;
            c->records[i].pinned=1;c->records[i].age=c->age;++c->hits;
        }else if((e->bytes<=c->capacity-c->used&&c->count<DOK_TEXTURE_CACHE_SLOTS)||first+1u==count){
            dok_status status=dok_frame_store_require(s,c,e);if(status!=DOK_STATUS_OK)return status;
        }else break;
    }
    if(first==count)return DOK_STATUS_OK;
    N23_TIME(start);
    dok_texture_cache *p=&w->planned;*p=*c;
    for(uint32_t i=0;i<p->count;++i){w->original_offsets[i]=p->records[i].offset;w->sources[i]=NULL;}
    uint32_t misses=0,decoded_bytes=0;
#if N23_CACHE_REPLACE
    uint32_t victim_offset=0,victim_bytes=0;
#endif
    for(uint32_t n=first;n<count;++n){const dok_asset_entry *e=entries[n];
        if(!e||e->bytes>p->capacity)goto fallback;
        uint32_t i;for(i=0;i<p->count;++i)if(p->records[i].key==e->id)break;
        if(i<p->count){if(p->records[i].bytes!=e->bytes)goto fallback;p->records[i].pinned=1;p->records[i].age=p->age;++p->hits;continue;}
        const dok_frame_record *source=NULL;
        for(i=0;i<s->count;++i)if(s->records[i].id==e->id){source=&s->records[i];break;}
        if(!source||source->decoded_bytes!=e->bytes||!packed_valid(s,source))goto fallback;
        while(e->bytes>p->capacity-p->used||p->count==DOK_TEXTURE_CACHE_SLOTS){
            uint32_t oldest=UINT32_MAX;
            for(i=0;i<p->count;++i)if(!p->records[i].pinned&&(oldest==UINT32_MAX||p->records[i].age<p->records[oldest].age))oldest=i;
            if(oldest==UINT32_MAX)goto fallback;
#if N23_CACHE_REPLACE
            if(p->evictions==c->evictions){victim_offset=w->original_offsets[oldest];victim_bytes=p->records[oldest].bytes;}
#endif
            uint32_t offset=p->records[oldest].offset,bytes=p->records[oldest].bytes;
            for(i=0;i<p->count;++i)if(p->records[i].offset>offset)p->records[i].offset-=bytes;
            --p->count;p->records[oldest]=p->records[p->count];
            w->original_offsets[oldest]=w->original_offsets[p->count];w->sources[oldest]=w->sources[p->count];
            p->used-=bytes;++p->evictions;
        }
        i=p->count++;p->records[i]=(dok_texture_cache_record){e->id,p->used,e->bytes,p->age,1};
        w->original_offsets[i]=UINT32_MAX;w->sources[i]=source;p->used+=e->bytes;++p->misses;++misses;decoded_bytes+=e->bytes;
    }
    /* Provenance travels through swap-removal: an initially cached ID evicted
     * and later requested is NEW, even if the final key existed at entry. */
    uint32_t old_count=0;
#if N23_CACHE_REPLACE
    uint32_t replacement=0;
    if(misses==1u&&p->evictions-c->evictions==1u){
        for(uint32_t i=0;i<p->count;++i)if(w->sources[i]&&p->records[i].bytes==victim_bytes&&replacement_source_ok(s,w->sources[i],c)){
            p->records[i].offset=victim_offset;replacement=1u;break;
        }
    }
    if(replacement){
        for(uint32_t i=0;i<p->count;++i)if(!w->sources[i])p->records[i].offset=w->original_offsets[i];
    }else
#endif
    {
    for(uint32_t i=0;i<p->count;++i)if(!w->sources[i]){
        uint32_t at=old_count;
        while(at&&w->original_offsets[w->order[at-1u]]>w->original_offsets[i]){w->order[at]=w->order[at-1u];--at;}
        w->order[at]=(uint16_t)i;++old_count;
    }
    for(uint32_t n=0;n<old_count;++n){uint32_t i=w->order[n],from=w->original_offsets[i],to=p->records[i].offset,size=p->records[i].bytes;
        if(from!=to){memmove(c->pool+to,c->pool+from,size);N23_ADD(4,size);}
    }
    }
    N23_ADD(0,dok_n23_profile_time()-start);N23_ADD(5,p->evictions-c->evictions);
    for(uint32_t i=0;i<p->count;++i)if(w->sources[i]){
        const dok_frame_record *r=w->sources[i];N23_TIME(decode_start);
        /* Every source was shape-validated before the first payload write. */
        dok_status status=dok_frame_unpack(s->pool+r->offset,r->bytes,c->pool+p->records[i].offset,r->decoded_bytes);
        N23_ADD(1,dok_n23_profile_time()-decode_start);N23_ADD(6,1u);
        if(status!=DOK_STATUS_OK)return status;
    }
    *c=*p;s->decodes+=misses;s->decoded_total+=decoded_bytes;
    return DOK_STATUS_OK;
fallback:
    return require_sequence(s,c,entries+first,count-first);
}
