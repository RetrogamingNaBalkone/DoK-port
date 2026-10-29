#include "dok/assets.h"
#include "dok/sha256.h"
#include <stddef.h>
#include <string.h>

#define SESSION_TAG UINT32_C(0x31415344)
#define CACHE_TAG UINT32_C(0x43414348)

static uint16_t le16(const unsigned char *p)
{ return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1]<<8)); }
static uint32_t le32(const unsigned char *p)
{ return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static int zero(const unsigned char *p, uint32_t n)
{
    uint32_t i;
    for(i=0;i<n;++i) if(p[i]!=0u) return 0;
    return 1;
}
static int live(const dok_asset_session *s)
{ return s!=NULL && s->_tag==SESSION_TAG && s->_reader.read_at!=NULL; }
static uint32_t make_id(uint32_t language,uint32_t source,uint32_t rep,uint32_t part)
{ return (language<<30)|(source<<18)|(rep<<10)|part; }
static dok_asset_status read_exact(const dok_asset_reader *r,uint32_t offset,
                                  unsigned char *dst,uint32_t count)
{
    uint32_t actual=0;
    dok_asset_status result;
    if(count>DOK_ASSET_READ_MAX || offset>r->file_bytes || count>r->file_bytes-offset)
        return DOK_ASSET_BOUNDS;
    result=r->read_at(r->context,offset,dst,count,&actual);
    if(result!=DOK_ASSET_OK) return DOK_ASSET_IO_ERROR;
    if(actual!=count) return DOK_ASSET_SHORT_READ;
    return DOK_ASSET_OK;
}
static void decode(const unsigned char *p,dok_asset_entry *e)
{
    e->id=le32(p);e->source_id=le32(p+4);e->offset=le32(p+8);e->bytes=le32(p+12);
    e->representation=le16(p+16);e->language=p[18];e->source_kind=p[19];
    e->source_index=le16(p+20);e->subindex=le16(p+22);
    e->width=le16(p+24);e->height=le16(p+26);
    e->storage_width=le16(p+28);e->storage_height=le16(p+30);
    e->param0=le32(p+32);e->param1=le32(p+36);
    memcpy(e->sha256,p+40,32);memcpy(e->source_sha256,p+72,32);
    memcpy(e->name_cp866,p+104,16);
}
static uint32_t pot(uint32_t n)
{
    uint32_t p=8;
    while(p<n && p<1024u) p*=2u;
    return p;
}
static int geometry_zero(const dok_asset_entry *e)
{ return e->width==0u && e->height==0u && e->storage_width==0u && e->storage_height==0u; }
static int same_geometry(const dok_asset_entry *e)
{ return e->width==e->storage_width && e->height==e->storage_height; }
static int parameters_zero(const dok_asset_entry *e)
{ return e->param0==0u && e->param1==0u; }

/* Checks only catalog fields; payload shape/link checks follow its own SHA. */
static int entry_shape(const dok_asset_entry *e)
{
    uint32_t part=0, i, name_end=16u;
    uint32_t rep=e->representation;
    if(e->language<1u || e->language>2u || e->source_index>4095u ||
       rep<1u || rep>16u || e->source_kind<1u || e->source_kind>8u || e->bytes==0u)
        return 0;
    if(e->subindex!=UINT16_MAX) {
        if(e->subindex>1023u) return 0;
        part=e->subindex;
    }
    if(e->id!=make_id(e->language,e->source_index,rep,part) ||
       e->source_id!=make_id(e->language,e->source_index,1u,0u)) return 0;
    for(i=0;i<16u;++i) if(e->name_cp866[i]==0u) {name_end=i;break;}
    if(name_end==0u || name_end==16u || !zero(e->name_cp866+name_end,16u-name_end)) return 0;
    if(rep==DOK_ASSET_FRAME_INDEX8 || rep==DOK_ASSET_PCM_CHUNK ||
       (rep==DOK_ASSET_GPU_INDEX8_POT && e->source_kind==2u)) {
        if(e->subindex==UINT16_MAX) return 0;
    } else if(e->subindex!=UINT16_MAX) return 0;
    switch(rep) {
    case DOK_ASSET_RAW_SOURCE:
        return e->source_id==e->id && geometry_zero(e) && parameters_zero(e) &&
               memcmp(e->sha256,e->source_sha256,32)==0;
    case DOK_ASSET_IMAGE_INDEX8:
    case DOK_ASSET_FRAME_INDEX8:
        return e->source_kind==(rep==2u?1u:2u) && e->width!=0u && e->height!=0u &&
            e->width<=1024u && e->height<=1024u && same_geometry(e) &&
            e->bytes==(uint32_t)e->width*e->height && e->param0<=65535u &&
            e->param1==(rep==2u?0u:8u);
    case DOK_ASSET_GPU_INDEX8_POT:
        return (e->source_kind==1u || e->source_kind==2u) &&
            e->width!=0u && e->height!=0u && e->width<=1024u && e->height<=1024u &&
            e->storage_width==pot(e->width) && e->storage_height==pot(e->height) &&
            e->bytes==(uint32_t)e->storage_width*e->storage_height &&
            e->param0<=65535u && e->param1==(e->source_kind==1u?0u:8u);
    case DOK_ASSET_STATIC_INDEX8:
    case DOK_ASSET_STATIC_GPU_INDEX8:
        return e->source_kind==1u && e->width==256u && e->height==192u &&
            e->storage_width==256u && e->storage_height==(rep==6u?192u:256u) &&
            e->bytes==256u*e->storage_height && e->param0!=0u && e->param1==1u;
    case DOK_ASSET_PALETTE_RGB555:
        return e->source_kind==4u && geometry_zero(e) && parameters_zero(e) && e->bytes==512u;
    case DOK_ASSET_ANIM_META:
        return e->source_kind==2u && geometry_zero(e) && parameters_zero(e) &&
               e->bytes>=24u && e->bytes<=8u+1024u*16u && (e->bytes-8u)%16u==0u;
    case DOK_ASSET_PCM_CLIP:
        return e->source_kind==8u && geometry_zero(e) && e->param0>0u &&
            e->param0<=65535u && e->param1>0u && e->bytes>=36u &&
            e->bytes<=24u+1024u*12u && (e->bytes-24u)%12u==0u;
    case DOK_ASSET_PCM_CHUNK:
        return e->source_kind==8u && geometry_zero(e) && e->bytes<=65536u &&
            e->param0==(uint32_t)e->subindex*65536u && e->param1>0u && e->param1<=65535u;
    case DOK_ASSET_TEXT_UTF8:
        return e->source_kind==7u && geometry_zero(e) && e->param0==65001u && e->param1==0u;
    case DOK_ASSET_FONT_BITMAP:
        return e->source_kind==3u && e->width==8u &&
            (e->height==8u || e->height==16u) && same_geometry(e) &&
            e->bytes==256u*e->height && e->param0==256u && e->param1==1u;
    case DOK_ASSET_LIGHT_TABLE:
        return e->source_kind==5u && e->width==256u && e->height==16u &&
            same_geometry(e) && e->bytes==4096u && parameters_zero(e);
    case DOK_ASSET_LEVEL_MAPS:
        return e->source_kind==6u && e->width!=0u && e->height!=0u && same_geometry(e) &&
            (uint64_t)e->bytes==20u+(uint64_t)e->width*e->height*3u &&
            e->param0!=0u && e->param1!=0u;
    case DOK_ASSET_LEVEL_CP866:
    case DOK_ASSET_TEXT_CP866:
        return e->source_kind==(rep==14u?6u:7u) && geometry_zero(e) &&
               e->param0==866u && e->param1==0u;
    default:return 0;
    }
}

dok_asset_status dok_assets_open(const dok_asset_reader *r,dok_asset_workspace *w,
                                dok_asset_session *out)
{
    static const unsigned char magic[8]={'D','O','K','N','D','S','1',0};
    unsigned char header[160], wire[128], digest[32];
    dok_asset_session candidate;
    dok_asset_entry e, source;
    dok_sha256 hash;
    dok_asset_status status;
    uint32_t count, data_offset, i,j, unique=0u,last_id=0u,end;
    memset(&source,0,sizeof(source));
    if(r==NULL || w==NULL || out==NULL || r->read_at==NULL) return DOK_ASSET_INVALID_ARGUMENT;
    if(r->file_bytes<160u) return DOK_ASSET_TRUNCATED;
    status=read_exact(r,0,header,160);if(status!=DOK_ASSET_OK) return status;
    if(memcmp(header,magic,8)!=0) return DOK_ASSET_BAD_HEADER;
    if(le16(header+8)!=1u) return DOK_ASSET_UNSUPPORTED_VERSION;
    if(le16(header+10)!=160u || le16(header+12)!=128u || le16(header+14)!=0u)
        return DOK_ASSET_BAD_HEADER;
    count=le32(header+16);data_offset=le32(header+24);
    if(count==0u || count>DOK_ASSET_MAX_ENTRIES || le32(header+20)!=160u ||
       data_offset!=160u+count*128u) return DOK_ASSET_BAD_HEADER;
    if(le32(header+28)>r->file_bytes || data_offset>r->file_bytes) return DOK_ASSET_TRUNCATED;
    if(le32(header+28)!=r->file_bytes) return DOK_ASSET_BAD_HEADER;
    dok_sha256_begin(&hash);dok_sha256_update(&hash,header,128);dok_sha256_end(&hash,digest);
    if(memcmp(digest,header+128,32)!=0) return DOK_ASSET_HASH_MISMATCH;
    end=data_offset;
    dok_sha256_begin(&hash);
    for(i=0;i<count;++i) {
        status=read_exact(r,160u+i*128u,wire,128);if(status!=DOK_ASSET_OK) return status;
        dok_sha256_update(&hash,wire,128);decode(wire,&e);
        if(!zero(wire+120,8) || !entry_shape(&e) || (i!=0u && e.id<=last_id))
            return DOK_ASSET_BAD_CATALOG;
        last_id=e.id;
        if(e.representation==DOK_ASSET_RAW_SOURCE) source=e;
        if(source.id!=e.source_id || e.source_kind!=source.source_kind ||
           memcmp(e.source_sha256,source.sha256,32)!=0 ||
           memcmp(e.name_cp866,source.name_cp866,16)!=0) return DOK_ASSET_BAD_CATALOG;
        if(e.offset<data_offset || (e.offset&31u)!=0u || e.offset>r->file_bytes ||
           e.bytes>r->file_bytes-e.offset) return DOK_ASSET_BOUNDS;
        for(j=0;j<unique;++j) {
            const dok_asset_range *a=&w->ranges[j];
            if(e.offset==a->offset && e.bytes==a->bytes && memcmp(e.sha256,a->sha256,32)==0)
                break;
            if(e.offset<a->offset+a->bytes && a->offset<e.offset+e.bytes)
                return DOK_ASSET_BAD_CATALOG;
        }
        if(j==unique) {
            uint32_t aligned;
            if(end>UINT32_MAX-31u) return DOK_ASSET_BOUNDS;
            aligned=(end+31u)&~31u;
            if(e.offset!=aligned) return DOK_ASSET_BAD_CATALOG;
            if(aligned!=end) {
                status=read_exact(r,end,w->io,aligned-end);if(status!=DOK_ASSET_OK) return status;
                if(!zero(w->io,aligned-end)) return DOK_ASSET_BAD_CATALOG;
            }
            w->ranges[unique].offset=e.offset;w->ranges[unique].bytes=e.bytes;
            memcpy(w->ranges[unique].sha256,e.sha256,32);++unique;end=e.offset+e.bytes;
        }
    }
    dok_sha256_end(&hash,digest);
    if(memcmp(digest,header+32,32)!=0) return DOK_ASSET_HASH_MISMATCH;
    if(end!=r->file_bytes) return DOK_ASSET_BAD_CATALOG;
    memset(&candidate,0,sizeof(candidate));candidate._reader=*r;
    candidate._tag=SESSION_TAG;candidate._entry_count=count;candidate._data_offset=data_offset;
    memcpy(candidate._directory_sha256,header+32,32);
    memcpy(candidate._source_sha256,header+64,64);
    *out=candidate;return DOK_ASSET_OK;
}

uint32_t dok_assets_count(const dok_asset_session *s)
{ return live(s)?s->_entry_count:0u; }
dok_asset_status dok_assets_catalog(const dok_asset_session *s,dok_asset_catalog *out)
{
    dok_asset_catalog value;
    if(!live(s) || out==NULL) return DOK_ASSET_INVALID_ARGUMENT;
    value.identity=le32(s->_directory_sha256);
    if(value.identity==0u) value.identity=1u;
    value.revision=1u;value.implementation=s;*out=value;return DOK_ASSET_OK;
}
dok_asset_status dok_assets_entry_at(const dok_asset_session *s,uint32_t index,dok_asset_entry *out)
{
    unsigned char wire[128];dok_asset_entry e;dok_asset_status status;
    if(!live(s) || out==NULL) return DOK_ASSET_INVALID_ARGUMENT;
    if(index>=s->_entry_count) return DOK_ASSET_NOT_FOUND;
    status=read_exact(&s->_reader,160u+index*128u,wire,128);
    if(status!=DOK_ASSET_OK) return status;
    decode(wire,&e);*out=e;return DOK_ASSET_OK;
}
dok_asset_status dok_assets_find(const dok_asset_session *s,dok_asset_id id,dok_asset_entry *out)
{
    uint32_t lo=0,hi;dok_asset_entry e;dok_asset_status status;
    if(!live(s) || out==NULL || id==0u) return DOK_ASSET_INVALID_ARGUMENT;
    hi=s->_entry_count;
    while(lo<hi) {
        uint32_t mid=lo+(hi-lo)/2u;
        status=dok_assets_entry_at(s,mid,&e);if(status!=DOK_ASSET_OK) return status;
        if(e.id==id) {*out=e;return DOK_ASSET_OK;}
        if(e.id<id) lo=mid+1u;else hi=mid;
    }
    return DOK_ASSET_NOT_FOUND;
}
dok_asset_status dok_assets_verify_hash(const dok_asset_session *s,dok_asset_id id,
                                       dok_asset_workspace *w)
{
    dok_asset_entry e;dok_sha256 hash;unsigned char digest[32];uint32_t done=0;
    dok_asset_status status;
    if(w==NULL) return DOK_ASSET_INVALID_ARGUMENT;
    status=dok_assets_find(s,id,&e);if(status!=DOK_ASSET_OK) return status;
    dok_sha256_begin(&hash);
    while(done<e.bytes) {
        uint32_t n=e.bytes-done;if(n>sizeof(w->io)) n=(uint32_t)sizeof(w->io);
        status=read_exact(&s->_reader,e.offset+done,w->io,n);if(status!=DOK_ASSET_OK) return status;
        dok_sha256_update(&hash,w->io,n);done+=n;
    }
    dok_sha256_end(&hash,digest);
    return memcmp(digest,e.sha256,32)==0?DOK_ASSET_OK:DOK_ASSET_HASH_MISMATCH;
}

static int utf8(const unsigned char *p,uint32_t count)
{
    uint32_t i=0;
    while(i<count) {
        uint32_t ch=p[i++],n,minimum;
        if(ch<0x80u) continue;
        if(ch>=0xc2u && ch<=0xdfu) {n=1;minimum=0x80u;ch&=0x1fu;}
        else if(ch>=0xe0u && ch<=0xefu) {n=2;minimum=0x800u;ch&=15u;}
        else if(ch>=0xf0u && ch<=0xf4u) {n=3;minimum=0x10000u;ch&=7u;}
        else return 0;
        if(n>count-i) return 0;
        while(n--!=0u) {uint32_t b=p[i++];if((b&0xc0u)!=0x80u) return 0;ch=(ch<<6)|(b&63u);}
        if(ch<minimum || ch>0x10ffffu || (ch>=0xd800u && ch<=0xdfffu)) return 0;
    }
    return 1;
}
static dok_asset_status linked(const dok_asset_session *s,uint32_t id,dok_asset_entry *e)
{
    dok_asset_status result=dok_assets_find(s,id,e);
    return result==DOK_ASSET_NOT_FOUND?DOK_ASSET_MALFORMED_PAYLOAD:result;
}
static dok_asset_status payload_shape(const dok_asset_session *s,const dok_asset_entry *e,
                                      const unsigned char *p)
{
    uint32_t i;
    dok_asset_entry child;
    dok_asset_status status;
    if(!entry_shape(e)) return DOK_ASSET_MALFORMED_PAYLOAD;
    switch(e->representation) {
    case DOK_ASSET_GPU_INDEX8_POT:
    case DOK_ASSET_STATIC_GPU_INDEX8:
        for(i=0;i<e->storage_height;++i) {
            uint32_t from=i<e->height?e->width:0u;
            if(!zero(p+i*e->storage_width+from,e->storage_width-from))
                return DOK_ASSET_MALFORMED_PAYLOAD;
        }
        if(e->representation==DOK_ASSET_GPU_INDEX8_POT) break;
        /* STATIC_GPU also validates its palette reference. */
        /* fall through */
    case DOK_ASSET_STATIC_INDEX8:
        status=linked(s,e->param0,&child);if(status!=DOK_ASSET_OK) return status;
        if(child.representation!=DOK_ASSET_PALETTE_RGB555 || child.language!=e->language)
            return DOK_ASSET_MALFORMED_PAYLOAD;
        break;
    case DOK_ASSET_PALETTE_RGB555:
        for(i=0;i<256u;++i) if((le16(p+i*2u)&0x8000u)!=0u) return DOK_ASSET_MALFORMED_PAYLOAD;
        break;
    case DOK_ASSET_ANIM_META: {
        uint32_t count=le16(p);
        if(count==0u || count>1024u || e->bytes!=8u+16u*count || le32(p+4)!=0u)
            return DOK_ASSET_MALFORMED_PAYLOAD;
        for(i=0;i<count;++i) {
            const unsigned char *record=p+8u+i*16u;
            if(le32(record)!=make_id(e->language,e->source_index,4u,i))
                return DOK_ASSET_MALFORMED_PAYLOAD;
            status=linked(s,le32(record),&child);if(status!=DOK_ASSET_OK) return status;
            if(child.representation!=DOK_ASSET_FRAME_INDEX8 || child.source_id!=e->source_id ||
               child.subindex!=i || child.param0!=le16(record+10) ||
               child.width!=le16(record+12) || child.height!=le16(record+14))
                return DOK_ASSET_MALFORMED_PAYLOAD;
        }
        break;
    }
    case DOK_ASSET_PCM_CLIP: {
        uint32_t count=le32(p+8),samples=le32(p+4),offset=0;
        if(le32(p)!=e->param0 || samples!=e->param1 || count==0u || count>1024u ||
           count!=(samples-1u)/65536u+1u || e->bytes!=24u+count*12u)
            return DOK_ASSET_MALFORMED_PAYLOAD;
        for(i=0;i<count;++i) {
            const unsigned char *record=p+24u+i*12u;
            uint32_t n=samples-offset;if(n>65536u) n=65536u;
            if(le32(record)!=make_id(e->language,e->source_index,9u,i) ||
               le32(record+4)!=offset || le32(record+8)!=n) return DOK_ASSET_MALFORMED_PAYLOAD;
            status=linked(s,le32(record),&child);if(status!=DOK_ASSET_OK) return status;
            if(child.representation!=DOK_ASSET_PCM_CHUNK || child.source_id!=e->source_id ||
               child.param0!=offset || child.param1!=e->param0 || child.bytes!=n)
                return DOK_ASSET_MALFORMED_PAYLOAD;
            offset+=n;
        }
        break;
    }
    case DOK_ASSET_PCM_CHUNK:
        status=linked(s,e->source_id,&child);if(status!=DOK_ASSET_OK) return status;
        if(child.bytes<=36u || e->param0>child.bytes-36u ||
           e->bytes!=((child.bytes-36u-e->param0)>65536u?65536u:child.bytes-36u-e->param0))
            return DOK_ASSET_MALFORMED_PAYLOAD;
        break;
    case DOK_ASSET_TEXT_UTF8:
        if(!utf8(p,e->bytes)) return DOK_ASSET_MALFORMED_PAYLOAD;
        break;
    case DOK_ASSET_LEVEL_MAPS:
        if(le16(p)!=e->width || le16(p+2)!=e->height || le16(p+4)!=1u ||
           le16(p+6)!=3u || le32(p+8)!=e->param0 || le32(p+12)!=e->param1 || le32(p+16)!=0u)
            return DOK_ASSET_MALFORMED_PAYLOAD;
        break;
    default:break;
    }
    return DOK_ASSET_OK;
}
dok_asset_status dok_assets_load(const dok_asset_session *s,dok_asset_id id,
    unsigned char *candidate,uint32_t capacity,dok_asset_view *out)
{
    dok_asset_entry e;dok_asset_view view;dok_sha256 hash;unsigned char digest[32];
    uint32_t done=0;dok_asset_status status;
    if(candidate==NULL || out==NULL || capacity>DOK_NDS_ASSETS_BYTES)
        return DOK_ASSET_INVALID_ARGUMENT;
    status=dok_assets_find(s,id,&e);if(status!=DOK_ASSET_OK) return status;
    if(e.representation==DOK_ASSET_RAW_SOURCE) return DOK_ASSET_VERIFY_ONLY;
    if(e.bytes>capacity || e.bytes>DOK_NDS_ASSETS_BYTES) return DOK_ASSET_NO_SPACE;
    dok_sha256_begin(&hash);
    while(done<e.bytes) {
        uint32_t n=e.bytes-done;if(n>DOK_ASSET_READ_MAX) n=DOK_ASSET_READ_MAX;
        status=read_exact(&s->_reader,e.offset+done,candidate+done,n);
        if(status!=DOK_ASSET_OK) return status;
        dok_sha256_update(&hash,candidate+done,n);done+=n;
    }
    dok_sha256_end(&hash,digest);
    if(memcmp(digest,e.sha256,32)!=0) return DOK_ASSET_HASH_MISMATCH;
    status=payload_shape(s,&e,candidate);if(status!=DOK_ASSET_OK) return status;
    view.entry=e;view.data=candidate;*out=view;return DOK_ASSET_OK;
}

static int overlap(const void *a,uint32_t na,const void *b,uint32_t nb)
{
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    if(na>UINTPTR_MAX-x || nb>UINTPTR_MAX-y) return 1;
    return x<y+nb && y<x+na;
}
static int slot_live(const dok_asset_cache_slot *s)
{ return s!=NULL && s->_valid==CACHE_TAG && s->_backing!=NULL && s->_capacity!=0u; }
dok_asset_status dok_asset_cache_initialize(dok_asset_cache_slot *s,unsigned char *backing,uint32_t capacity)
{
    dok_asset_cache_slot value;
    if(s==NULL || backing==NULL || capacity==0u || capacity>DOK_NDS_ASSETS_BYTES ||
       overlap(s,(uint32_t)sizeof(*s),backing,capacity)) return DOK_ASSET_INVALID_ARGUMENT;
    memset(&value,0,sizeof(value));value._backing=backing;value._capacity=capacity;
    value._generation=1u;value._valid=CACHE_TAG;*s=value;return DOK_ASSET_OK;
}
dok_asset_status dok_asset_cache_acquire(dok_asset_cache_slot *s,dok_asset_lease *out)
{
    dok_asset_lease value;
    if(!slot_live(s) || out==NULL || overlap(s,(uint32_t)sizeof(*s),out,(uint32_t)sizeof(*out)) ||
       overlap(s->_backing,s->_capacity,out,(uint32_t)sizeof(*out))) return DOK_ASSET_INVALID_ARGUMENT;
    if(s->_entry.id==0u) return DOK_ASSET_INVALID_STATE;
    if(s->_pins==UINT32_MAX) return DOK_ASSET_LIMIT_REACHED;
    value.slot=s;value.generation=s->_generation;value.view.entry=s->_entry;
    value.view.data=s->_backing;++s->_pins;*out=value;return DOK_ASSET_OK;
}
dok_asset_status dok_asset_cache_replace(const dok_asset_session *session,dok_asset_id id,
    dok_asset_cache_slot *s,unsigned char *candidate,uint32_t capacity,dok_asset_lease *out)
{
    dok_asset_view view;dok_asset_status status;
    if(!slot_live(s) || candidate==NULL || out==NULL ||
       overlap(s->_backing,s->_capacity,candidate,capacity) ||
       overlap(s,(uint32_t)sizeof(*s),candidate,capacity) ||
       overlap(out,(uint32_t)sizeof(*out),candidate,capacity) ||
       overlap(s,(uint32_t)sizeof(*s),out,(uint32_t)sizeof(*out)) ||
       overlap(s->_backing,s->_capacity,out,(uint32_t)sizeof(*out)) ||
       (uint64_t)s->_capacity+capacity>DOK_NDS_ASSETS_BYTES) return DOK_ASSET_INVALID_ARGUMENT;
    if(s->_pins!=0u) return DOK_ASSET_PINNED;
    if(s->_generation==UINT32_MAX) return DOK_ASSET_LIMIT_REACHED;
    status=dok_assets_find(session,id,&view.entry);if(status!=DOK_ASSET_OK) return status;
    if(view.entry.bytes>s->_capacity) return DOK_ASSET_NO_SPACE;
    status=dok_assets_load(session,id,candidate,capacity,&view);if(status!=DOK_ASSET_OK) return status;
    memcpy(s->_backing,candidate,view.entry.bytes);s->_entry=view.entry;++s->_generation;
    return dok_asset_cache_acquire(s,out);
}
dok_asset_status dok_asset_cache_release(dok_asset_lease *lease)
{
    dok_asset_cache_slot *s;
    if(lease==NULL || lease->slot==NULL) return DOK_ASSET_INVALID_ARGUMENT;
    s=lease->slot;
    if(!slot_live(s) || s->_generation!=lease->generation || s->_pins==0u ||
       s->_entry.id!=lease->view.entry.id || s->_backing!=lease->view.data)
        return DOK_ASSET_STALE_LEASE;
    --s->_pins;memset(lease,0,sizeof(*lease));return DOK_ASSET_OK;
}
