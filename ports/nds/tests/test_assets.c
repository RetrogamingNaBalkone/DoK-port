#include "dok/assets.h"
#include "dok/sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned int checks;
#define CHECK(x) do { ++checks; if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 0; } } while(0)
static dok_asset_workspace workspace;
static unsigned char candidate[131072],backing[131072];
static unsigned char fixture[1696];
typedef struct Reader {
    const unsigned char *bytes;
    FILE *file;
    uint32_t size,fault_offset,mode,calls,max_request,bytes_read;
} Reader;
static uint32_t read32(const unsigned char *p)
{return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static void put16(unsigned char *p,uint16_t x)
{p[0]=(unsigned char)x;p[1]=(unsigned char)(x>>8);}
static void put32(unsigned char *p,uint32_t x)
{p[0]=(unsigned char)x;p[1]=(unsigned char)(x>>8);p[2]=(unsigned char)(x>>16);p[3]=(unsigned char)(x>>24);}
static void hash_bytes(const void *p,uint32_t count,unsigned char out[32])
{dok_sha256 h;dok_sha256_begin(&h);dok_sha256_update(&h,p,count);dok_sha256_end(&h,out);}
static dok_asset_status reader(void *context,uint32_t offset,unsigned char *out,uint32_t count,uint32_t *actual)
{
    Reader *r=context;
    ++r->calls;if(count>r->max_request) r->max_request=count;
    if(offset>r->size || count>r->size-offset) return DOK_ASSET_IO_ERROR;
    if(offset==r->fault_offset && r->mode!=0u) {
        if(r->mode==2u) return DOK_ASSET_IO_ERROR;
        *actual=r->mode==3u?count+1u:count-1u;
        return DOK_ASSET_OK;
    }
    if(r->file!=NULL) {
        if(fseek(r->file,(long)offset,SEEK_SET)!=0) return DOK_ASSET_IO_ERROR;
        *actual=(uint32_t)fread(out,1,count,r->file);
        if(ferror(r->file)) return DOK_ASSET_IO_ERROR;
    } else {memcpy(out,r->bytes+offset,count);*actual=count;}
    r->bytes_read+=*actual;return DOK_ASSET_OK;
}
static dok_asset_reader descriptor(Reader *r)
{dok_asset_reader v;v.context=r;v.read_at=reader;v.file_bytes=r->size;return v;}
static void rehash(unsigned char *p)
{hash_bytes(p+160,read32(p+16)*128u,p+32);hash_bytes(p,128,p+128);}
static void make_fixture(void)
{
    unsigned char *raw=fixture+160,*palette=fixture+288;
    uint32_t i;
    memset(fixture,0,sizeof(fixture));memcpy(fixture,"DOKNDS1",7);
    put16(fixture+8,1);put16(fixture+10,160);put16(fixture+12,128);
    put32(fixture+16,2);put32(fixture+20,160);put32(fixture+24,416);put32(fixture+28,sizeof(fixture));
    for(i=0;i<768u;++i) fixture[416u+i]=(unsigned char)(i%64u);
    for(i=0;i<256u;++i) put16(fixture+1184u+i*2u,(uint16_t)i);
    put32(raw,0x40000400u);put32(raw+4,0x40000400u);put32(raw+8,416);put32(raw+12,768);
    put16(raw+16,1);raw[18]=1;raw[19]=4;put16(raw+22,UINT16_MAX);
    hash_bytes(fixture+416,768,raw+40);memcpy(raw+72,raw+40,32);memcpy(raw+104,"PAL",3);
    memcpy(palette,raw,128);put32(palette,0x40001c00u);put32(palette+8,1184);
    put32(palette+12,512);put16(palette+16,7);hash_bytes(fixture+1184,512,palette+40);rehash(fixture);
}
static int failure_preserves(Reader *r,dok_asset_status expected)
{
    dok_asset_session out,before;dok_asset_reader d=descriptor(r);
    memset(&out,0xa5,sizeof(out));before=out;
    CHECK(dok_assets_open(&d,&workspace,&out)==expected);
    CHECK(memcmp(&out,&before,sizeof(out))==0);return 1;
}
static int unit_tests(void)
{
    static const unsigned char abc[32]={0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,
        0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,0xb0,0x03,0x61,0xa3,0x96,0x17,
        0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
    static const unsigned char empty[32]={0xe3,0xb0,0xc4,0x42,0x98,0xfc,0x1c,0x14,
        0x9a,0xfb,0xf4,0xc8,0x99,0x6f,0xb9,0x24,0x27,0xae,0x41,0xe4,0x64,0x9b,
        0x93,0x4c,0xa4,0x95,0x99,0x1b,0x78,0x52,0xb8,0x55};
    dok_sha256 h;unsigned char digest[32];
    Reader r={0};dok_asset_reader d;dok_asset_session s,before_session;
    dok_asset_view v,before_view;dok_asset_entry e;
    dok_asset_cache_slot slot,before_slot;dok_asset_lease lease,before_lease,second,stale;
    unsigned char old_backing[512];uint32_t i,calls;
    dok_sha256_begin(&h);dok_sha256_update(&h,"a",1);dok_sha256_update(&h,"bc",2);dok_sha256_end(&h,digest);
    CHECK(memcmp(digest,abc,32)==0);hash_bytes("",0,digest);CHECK(memcmp(digest,empty,32)==0);
    r.bytes=fixture;r.size=sizeof(fixture);make_fixture();d=descriptor(&r);
    CHECK(dok_assets_open(&d,&workspace,&s)==DOK_ASSET_OK);
    CHECK(dok_assets_count(&s)==2u);CHECK(dok_assets_entry_at(&s,1,&e)==DOK_ASSET_OK);
    CHECK(e.id==0x40001c00u && e.bytes==512u);
    CHECK(dok_assets_load(&s,e.id,candidate,sizeof(candidate),&v)==DOK_ASSET_OK);
    CHECK(memcmp(v.data,fixture+1184,512)==0);
    CHECK(dok_assets_verify_hash(&s,0x40000400u,&workspace)==DOK_ASSET_OK);
    memset(&v,0xa5,sizeof(v));before_view=v;
    CHECK(dok_assets_load(&s,0x40000400u,candidate,sizeof(candidate),&v)==DOK_ASSET_VERIFY_ONLY);
    CHECK(memcmp(&v,&before_view,sizeof(v))==0);
    CHECK(dok_assets_load(&s,e.id,candidate,511,&v)==DOK_ASSET_NO_SPACE);
    CHECK(memcmp(&v,&before_view,sizeof(v))==0);
    CHECK(dok_assets_find(&s,0x40002000u,&e)==DOK_ASSET_NOT_FOUND);
    CHECK(dok_assets_entry_at(&s,2,&e)==DOK_ASSET_NOT_FOUND);
    CHECK(dok_assets_find(&s,0,&e)==DOK_ASSET_INVALID_ARGUMENT);
    CHECK(dok_assets_open(NULL,&workspace,&s)==DOK_ASSET_INVALID_ARGUMENT);
    for(i=0;i<160u;++i) {r.size=i;CHECK(failure_preserves(&r,DOK_ASSET_TRUNCATED));}
    r.size=sizeof(fixture);r.fault_offset=0;
    for(i=1;i<=3u;++i) {r.mode=i;CHECK(failure_preserves(&r,i==2u?DOK_ASSET_IO_ERROR:DOK_ASSET_SHORT_READ));}
    r.mode=0;fixture[0]^=1;CHECK(failure_preserves(&r,DOK_ASSET_BAD_HEADER));make_fixture();
    put16(fixture+8,2);CHECK(failure_preserves(&r,DOK_ASSET_UNSUPPORTED_VERSION));make_fixture();
    fixture[128]^=1;CHECK(failure_preserves(&r,DOK_ASSET_HASH_MISMATCH));make_fixture();
    put32(fixture+16,2049);CHECK(failure_preserves(&r,DOK_ASSET_BAD_HEADER));make_fixture();
    r.size=sizeof(fixture)-1u;CHECK(failure_preserves(&r,DOK_ASSET_TRUNCATED));r.size=sizeof(fixture);
    fixture[160+120]=1;rehash(fixture);CHECK(failure_preserves(&r,DOK_ASSET_BAD_CATALOG));make_fixture();
    fixture[288+72]^=1;rehash(fixture);CHECK(failure_preserves(&r,DOK_ASSET_BAD_CATALOG));make_fixture();
    put32(fixture+288+8,0xffffffe0u);rehash(fixture);CHECK(failure_preserves(&r,DOK_ASSET_BOUNDS));make_fixture();
    put32(fixture+288+8,448);rehash(fixture);CHECK(failure_preserves(&r,DOK_ASSET_BAD_CATALOG));make_fixture();
    put32(fixture+288,read32(fixture+160));rehash(fixture);CHECK(failure_preserves(&r,DOK_ASSET_BAD_CATALOG));make_fixture();
    fixture[160+104]=0;rehash(fixture);CHECK(failure_preserves(&r,DOK_ASSET_BAD_CATALOG));make_fixture();
    fixture[32]^=1;hash_bytes(fixture,128,fixture+128);CHECK(failure_preserves(&r,DOK_ASSET_HASH_MISMATCH));make_fixture();
    d=descriptor(&r);CHECK(dok_assets_open(&d,&workspace,&s)==DOK_ASSET_OK);
    before_session=s;fixture[1184]^=1;
    CHECK(dok_assets_load(&s,0x40001c00u,candidate,sizeof(candidate),&v)==DOK_ASSET_HASH_MISMATCH);
    CHECK(memcmp(&v,&before_view,sizeof(v))==0 && memcmp(&s,&before_session,sizeof(s))==0);
    make_fixture();fixture[1185]|=0x80u;hash_bytes(fixture+1184,512,fixture+288+40);rehash(fixture);
    CHECK(dok_assets_open(&d,&workspace,&s)==DOK_ASSET_OK);
    CHECK(dok_assets_load(&s,0x40001c00u,candidate,sizeof(candidate),&v)==DOK_ASSET_MALFORMED_PAYLOAD);
    CHECK(memcmp(&v,&before_view,sizeof(v))==0);
    make_fixture();CHECK(dok_assets_open(&d,&workspace,&s)==DOK_ASSET_OK);
    CHECK(dok_asset_cache_initialize(&slot,backing,sizeof(backing))==DOK_ASSET_OK);
    CHECK(dok_asset_cache_acquire(&slot,&lease)==DOK_ASSET_INVALID_STATE);
    CHECK(dok_asset_cache_replace(&s,0x40001c00u,&slot,candidate,sizeof(candidate),&lease)==DOK_ASSET_OK);
    CHECK(slot._generation==2u && slot._pins==1u);
    CHECK(dok_asset_cache_acquire(&slot,&second)==DOK_ASSET_OK);
    before_slot=slot;calls=r.calls;memset(&before_lease,0xa5,sizeof(before_lease));
    CHECK(dok_asset_cache_replace(&s,0x40001c00u,&slot,candidate,sizeof(candidate),&before_lease)==DOK_ASSET_PINNED);
    CHECK(memcmp(&slot,&before_slot,sizeof(slot))==0 && calls==r.calls);
    CHECK(dok_asset_cache_release(&second)==DOK_ASSET_OK);stale=lease;
    CHECK(dok_asset_cache_release(&lease)==DOK_ASSET_OK);
    CHECK(dok_asset_cache_release(&lease)==DOK_ASSET_INVALID_ARGUMENT);
    memcpy(old_backing,backing,512);before_slot=slot;before_lease=lease;
    r.fault_offset=1184;r.mode=1;
    CHECK(dok_asset_cache_replace(&s,0x40001c00u,&slot,candidate,sizeof(candidate),&lease)==DOK_ASSET_SHORT_READ);
    CHECK(memcmp(&slot,&before_slot,sizeof(slot))==0 && memcmp(old_backing,backing,512)==0);
    CHECK(memcmp(&lease,&before_lease,sizeof(lease))==0);r.mode=0;
    fixture[1184]^=1;
    CHECK(dok_asset_cache_replace(&s,0x40001c00u,&slot,candidate,sizeof(candidate),&lease)==DOK_ASSET_HASH_MISMATCH);
    CHECK(memcmp(&slot,&before_slot,sizeof(slot))==0 && memcmp(old_backing,backing,512)==0);
    make_fixture();
    CHECK(dok_asset_cache_replace(&s,0x40001c00u,&slot,backing,sizeof(backing),&lease)==DOK_ASSET_INVALID_ARGUMENT);
    CHECK(dok_asset_cache_replace(&s,0x40001c00u,&slot,candidate,sizeof(candidate),&lease)==DOK_ASSET_OK);
    CHECK(dok_asset_cache_release(&stale)==DOK_ASSET_STALE_LEASE);
    CHECK(dok_asset_cache_release(&lease)==DOK_ASSET_OK);
    slot._generation=UINT32_MAX;before_slot=slot;
    CHECK(dok_asset_cache_replace(&s,0x40001c00u,&slot,candidate,sizeof(candidate),&lease)==DOK_ASSET_LIMIT_REACHED);
    CHECK(memcmp(&slot,&before_slot,sizeof(slot))==0);
    slot._pins=UINT32_MAX;
    CHECK(dok_asset_cache_acquire(&slot,&lease)==DOK_ASSET_LIMIT_REACHED);
    CHECK(r.max_request<=DOK_ASSET_READ_MAX);return 1;
}
/* Mutate an isolated host copy, never the original/package file. Re-sign every
 * exact payload alias and catalog/header so typed refusal cannot be explained
 * by the outer hash gate. This is test memory, not runtime package residency. */
static void resign_payload(unsigned char *data,uint32_t offset,uint32_t bytes)
{
    unsigned char digest[32];uint32_t i,count=read32(data+16);
    hash_bytes(data+offset,bytes,digest);
    for(i=0;i<count;++i) {
        unsigned char *wire=data+160u+i*128u;
        if(read32(wire+8)==offset && read32(wire+12)==bytes) memcpy(wire+40,digest,32);
    }
    rehash(data);
}
static int actual_malformed_tests(FILE *file,uint32_t size)
{
    unsigned char *copy=malloc(size);
    Reader r={0};dok_asset_reader d;dok_asset_session s;dok_asset_view out,before;
    uint32_t count,i,scenario;
    CHECK(copy!=NULL);r.bytes=copy;r.size=size;d=descriptor(&r);
    for(scenario=0;scenario<8u;++scenario) {
        uint32_t offset=0,bytes=0,rep=scenario==0u?3u:scenario==1u?8u:
            scenario==2u?5u:scenario==3u?13u:10u;
        CHECK(fseek(file,0,SEEK_SET)==0);CHECK(fread(copy,1,size,file)==size);count=read32(copy+16);
        for(i=0;i<count;++i) {
            unsigned char *wire=copy+160u+i*128u;
            if((uint32_t)(wire[16]|((uint32_t)wire[17]<<8))!=rep) continue;
            if(scenario==2u && wire[24]==wire[28] && wire[25]==wire[29] &&
               wire[26]==wire[30] && wire[27]==wire[31]) continue;
            offset=read32(wire+8);bytes=read32(wire+12);break;
        }
        CHECK(offset!=0u && i<count);
        if(scenario==0u) put32(copy+offset+8,read32(copy+offset+8)^0x40000u);
        else if(scenario==1u) put32(copy+offset+24u+4u,1u);
        else if(scenario==2u) {
            const unsigned char *wire=copy+160u+i*128u;
            uint32_t width=wire[24]|((uint32_t)wire[25]<<8),height=wire[26]|((uint32_t)wire[27]<<8);
            uint32_t sw=wire[28]|((uint32_t)wire[29]<<8);
            copy[offset+(width<sw?width:height*sw)]=1u;
        } else if(scenario==3u) put16(copy+offset+4u,2u);
        else {
            CHECK(bytes>=4u);
            if(scenario==4u) {copy[offset]=0xc0u;copy[offset+1u]=0x80u;}
            else if(scenario==5u) {copy[offset]=0xedu;copy[offset+1u]=0xa0u;copy[offset+2u]=0x80u;}
            else if(scenario==6u) {copy[offset]=0xf4u;copy[offset+1u]=0x90u;copy[offset+2u]=0x80u;copy[offset+3u]=0x80u;}
            else {copy[offset+bytes-1u]=0xe2u;}
        }
        resign_payload(copy,offset,bytes);
        CHECK(dok_assets_open(&d,&workspace,&s)==DOK_ASSET_OK);
        memset(&out,0xa5,sizeof(out));before=out;
        CHECK(dok_assets_load(&s,read32(copy+160u+i*128u),candidate,sizeof(candidate),&out)==DOK_ASSET_MALFORMED_PAYLOAD);
        CHECK(memcmp(&out,&before,sizeof(out))==0);
    }
    free(copy);return 1;
}
static int actual_tests(const char *path)
{
    Reader r={0};dok_asset_reader d;dok_asset_session s;
    dok_asset_entry e;dok_asset_view v;dok_asset_catalog catalog;
    uint32_t i,mask=0,raw_count=0,largest=0,long_samples=0,long_chunks=0,long_read=0;
    FILE *f=fopen(path,"rb");long size;
    CHECK(f!=NULL);CHECK(fseek(f,0,SEEK_END)==0);size=ftell(f);
    CHECK(size>0 && (uint64_t)size<=UINT32_MAX);r.file=f;r.size=(uint32_t)size;
    d=descriptor(&r);CHECK(dok_assets_open(&d,&workspace,&s)==DOK_ASSET_OK);
    CHECK(dok_assets_catalog(&s,&catalog)==DOK_ASSET_OK);
    CHECK(catalog.implementation==&s && catalog.identity!=0u && catalog.revision==1u);
    for(i=0;i<dok_assets_count(&s);++i) {
        CHECK(dok_assets_entry_at(&s,i,&e)==DOK_ASSET_OK);mask|=1u<<(e.representation-1u);
        if(e.representation==DOK_ASSET_RAW_SOURCE) {
            ++raw_count;CHECK(dok_assets_verify_hash(&s,e.id,&workspace)==DOK_ASSET_OK);
        } else {
            CHECK(dok_assets_load(&s,e.id,candidate,sizeof(candidate),&v)==DOK_ASSET_OK);
            CHECK(v.entry.id==e.id && v.data==candidate);
            if(e.bytes>largest) largest=e.bytes;
            if(e.representation==DOK_ASSET_PCM_CLIP && e.language==1u && e.param1==474361u) {
                uint32_t j,n=read32(candidate+8);unsigned char descriptor_copy[120];
                CHECK(e.bytes<=sizeof(descriptor_copy));memcpy(descriptor_copy,candidate,e.bytes);
                long_samples=e.param1;long_chunks=n;
                for(j=0;j<n;++j) {
                    uint32_t chunk_id=read32(descriptor_copy+24u+j*12u);
                    CHECK(dok_assets_load(&s,chunk_id,candidate,sizeof(candidate),&v)==DOK_ASSET_OK);
                    CHECK(v.entry.bytes<=65536u);long_read+=v.entry.bytes;
                }
            }
        }
    }
    CHECK(mask==0xffffu && raw_count==292u && largest==131072u);
    CHECK(long_samples==474361u && long_chunks==8u && long_read==long_samples);
    CHECK(r.max_request<=65536u);CHECK(actual_malformed_tests(f,r.size));CHECK(fclose(f)==0);
    printf("{\"success\":true,\"checks\":%u,\"entry_count\":%u,\"raw_count\":%u,"
           "\"type_mask\":%u,\"largest_payload\":%u,\"long_sample_count\":%u,"
           "\"long_chunks\":%u,\"long_read_samples\":%u,\"read_calls\":%u,"
           "\"read_bytes\":%u,\"max_request\":%u,\"workspace_bytes\":%u,"
           "\"test_asset_pool_bytes\":%u}\n",checks,(unsigned int)dok_assets_count(&s),(unsigned int)raw_count,(unsigned int)mask,
           (unsigned int)largest,(unsigned int)long_samples,(unsigned int)long_chunks,(unsigned int)long_read,
           (unsigned int)r.calls,(unsigned int)r.bytes_read,(unsigned int)r.max_request,
           (unsigned int)sizeof(workspace),(unsigned int)(sizeof(candidate)+sizeof(backing)));
    return 1;
}
int main(int argc,char **argv)
{
    if(argc!=2) {fprintf(stderr,"usage: test_assets <dok-assets.ndspak>\n");return 2;}
    if(!unit_tests() || !actual_tests(argv[1])) return 1;
    return 0;
}
