/* Independent N04 executable: read-only prepared package on DLDI/FAT.
 * It exercises the same strict-C11 reader used by host tests, not game logic. */
#include <nds.h>
#include <fat.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dok/assets.h"
#include "dok/sha256.h"
#include "assets_probe.h"
#include "file_reader.h"

#define CANDIDATE_BYTES 131072u
extern unsigned char __end__,__eheap_end;
_Alignas(32) volatile NdsAssetsProbe g_nds_assets_probe={
    .magic=NDS_ASSETS_MAGIC,.abi_version=NDS_ASSETS_ABI,.size_bytes=sizeof(NdsAssetsProbe)
};
static NdsAssetsProbe observation;
static PrintConsole top,bottom;
static NdsAssetFile package;
static dok_asset_session session;
static dok_asset_workspace *workspace;
static unsigned char *asset_pool,*audio_pool;
static uint32_t allocation_bytes;

static uint32_t address(const void *p) {return (uint32_t)(uintptr_t)p;}
static uint32_t read32(const unsigned char *p)
{return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static void check(uint32_t bit,int passed)
{if(passed) observation.tests_pass_mask|=1u<<bit;else observation.error_bits|=1u<<bit;}
static void publish(void)
{
    uint32_t sequence;
    ++g_nds_assets_probe.sequence;
    DC_FlushRange((const void *)&g_nds_assets_probe.sequence,sizeof(uint32_t));
    sequence=g_nds_assets_probe.sequence;
    observation.magic=NDS_ASSETS_MAGIC;observation.abi_version=NDS_ASSETS_ABI;
    observation.size_bytes=sizeof(observation);observation.sequence=sequence;
    observation.read_calls=package.calls;observation.read_bytes=package.bytes_read;
    observation.max_request=package.max_request;
    memcpy((void *)&g_nds_assets_probe,&observation,sizeof(observation));
    DC_FlushRange((const void *)&g_nds_assets_probe,sizeof(g_nds_assets_probe));
    ++g_nds_assets_probe.sequence;
    DC_FlushRange((const void *)&g_nds_assets_probe.sequence,sizeof(uint32_t));
}
static void progress(uint32_t case_id)
{observation.case_id=case_id;++observation.frame;publish();}

static int memory_setup(void)
{
    uint32_t before=address(getHeapLimit()),limit=address(&__eheap_end);
    unsigned char *unaligned;
    observation.heap_limit_before=before;observation.static_bytes=address(&__end__)-DOK_NDS_MAIN_BASE;
    observation.reserve_bytes=DOK_NDS_RESERVE_BYTES;
    if(isDSiMode() || limit!=DOK_NDS_ARM9_LIMIT || before<limit ||
       observation.static_bytes>DOK_NDS_STATIC_CEILING) return 0;
    if(before!=limit && reduceHeapSize(before-limit)!=0) return 0;
    if(reduceHeapSize(DOK_NDS_RESERVE_BYTES)!=0) return 0;
    observation.heap_limit_final=address(getHeapLimit());
    if(observation.heap_limit_final!=DOK_NDS_ARM9_LIMIT-DOK_NDS_RESERVE_BYTES) return 0;
    /* All candidate and published backing live inside this ONE asset pool.
     * The fixed partition used by this probe is not the production cache policy. */
    unaligned=malloc(DOK_NDS_ASSETS_BYTES+31u);
    workspace=malloc(DOK_NDS_IO_BYTES);audio_pool=malloc(DOK_NDS_AUDIO_BYTES);
    if(unaligned==NULL || workspace==NULL || audio_pool==NULL) return 0;
    asset_pool=(unsigned char *)(((uintptr_t)unaligned+31u)&~(uintptr_t)31u);
    allocation_bytes=DOK_NDS_ASSETS_BYTES+31u+DOK_NDS_IO_BYTES+DOK_NDS_AUDIO_BYTES;
    observation.asset_pool_bytes=DOK_NDS_ASSETS_BYTES;
    observation.asset_pool_base=address(asset_pool);
    observation.asset_highwater=CANDIDATE_BYTES*2u;
    observation.workspace_bytes=sizeof(*workspace);
    observation.sdk_heap_available=observation.heap_limit_final-address(getHeapEnd());
    return observation.sdk_heap_available>=DOK_NDS_SDK_HEAP_MIN;
}
static void typed_and_stream_checks(void)
{
    dok_asset_entry entry,selected[16];
    dok_asset_view view;
    uint32_t selected_mask=0,largest_id=0,long_id=0;
    int good=1;
    memset(selected,0,sizeof(selected));
    for(uint32_t i=0;i<dok_assets_count(&session);++i) {
        if(dok_assets_entry_at(&session,i,&entry)!=DOK_ASSET_OK) {good=0;break;}
        uint32_t bit=1u<<(entry.representation-1u);
        if((selected_mask&bit)==0u) {selected[entry.representation-1u]=entry;selected_mask|=bit;}
        if(entry.representation==DOK_ASSET_RAW_SOURCE) ++observation.raw_count;
        if(entry.representation==DOK_ASSET_GPU_INDEX8_POT && entry.bytes>observation.largest_gpu_bytes) {
            observation.largest_gpu_bytes=entry.bytes;largest_id=entry.id;
        }
        if(entry.representation==DOK_ASSET_PCM_CLIP && entry.language==1u && entry.param1==474361u)
            long_id=entry.id;
        if((i&63u)==0u) progress(1u);
    }
    for(uint32_t i=0;i<16u;++i) {
        if(selected[i].id==0u) {good=0;continue;}
        int both_languages=1;
        for(uint32_t language=1u;language<=2u;++language) {
            uint32_t id=(selected[i].id&UINT32_C(0x3fffffff))|(language<<30);
            dok_asset_status status=i==0u?dok_assets_verify_hash(&session,id,workspace):
                dok_assets_load(&session,id,asset_pool,CANDIDATE_BYTES,&view);
            if(status!=DOK_ASSET_OK) both_languages=0;
        }
        if(both_languages) observation.type_mask|=1u<<i;else good=0;
        progress(2u+i);
    }
    check(1u,good && observation.type_mask==0xffffu && observation.raw_count==292u);
    check(3u,largest_id!=0u && observation.largest_gpu_bytes==131072u &&
          dok_assets_load(&session,largest_id,asset_pool,CANDIDATE_BYTES,&view)==DOK_ASSET_OK);
    good=long_id!=0u && dok_assets_load(&session,long_id,asset_pool,CANDIDATE_BYTES,&view)==DOK_ASSET_OK;
    if(good) {
        unsigned char clip[120];
        good=view.entry.bytes<=sizeof(clip);
        if(good) {
            memcpy(clip,view.data,view.entry.bytes);
            observation.long_sample_count=read32(clip+4);observation.long_chunks=read32(clip+8);
            for(uint32_t i=0;i<observation.long_chunks;++i) {
                uint32_t id=read32(clip+24u+i*12u);
                if(dok_assets_load(&session,id,audio_pool,65536u,&view)!=DOK_ASSET_OK ||
                   view.entry.bytes>65536u) {good=0;break;}
                observation.long_read_samples+=view.entry.bytes;progress(20u+i);
            }
        }
    }
    check(2u,good && observation.long_sample_count==474361u && observation.long_chunks==8u &&
          observation.long_read_samples==474361u);
    progress(30u);
}
static void cache_checks(void)
{
    dok_asset_cache_slot slot,before;
    dok_asset_lease lease,out,sentinel;
    dok_asset_entry entry;
    unsigned char digest[32],after[32];dok_sha256 hash;
    unsigned char *candidate=asset_pool,*backing=asset_pool+CANDIDATE_BYTES;
    int good=0;
    for(uint32_t i=0;i<dok_assets_count(&session);++i) {
        if(dok_assets_entry_at(&session,i,&entry)!=DOK_ASSET_OK) break;
        if(entry.representation==DOK_ASSET_PALETTE_RGB555) {good=1;break;}
    }
    good=good && dok_asset_cache_initialize(&slot,backing,CANDIDATE_BYTES)==DOK_ASSET_OK &&
         dok_asset_cache_replace(&session,entry.id,&slot,candidate,CANDIDATE_BYTES,&lease)==DOK_ASSET_OK;
    if(!good) {check(4u,0);check(5u,0);return;}
    before=slot;memset(&out,0xa5,sizeof(out));sentinel=out;
    observation.pinned_refusal=dok_asset_cache_replace(&session,entry.id,&slot,candidate,CANDIDATE_BYTES,&out);
    check(4u,observation.pinned_refusal==DOK_ASSET_PINNED &&
          memcmp(&slot,&before,sizeof(slot))==0 && memcmp(&out,&sentinel,sizeof(out))==0);
    good=dok_asset_cache_release(&lease)==DOK_ASSET_OK;before=slot;
    dok_sha256_begin(&hash);dok_sha256_update(&hash,backing,entry.bytes);dok_sha256_end(&hash,digest);
    package.test_fault_mode=1u;package.test_fault_offset=entry.offset;
    good=good && dok_asset_cache_replace(&session,entry.id,&slot,candidate,CANDIDATE_BYTES,&out)==DOK_ASSET_SHORT_READ;
    package.test_fault_mode=0;
    dok_sha256_begin(&hash);dok_sha256_update(&hash,backing,entry.bytes);dok_sha256_end(&hash,after);
    check(5u,good && memcmp(&slot,&before,sizeof(slot))==0 && memcmp(digest,after,32)==0 &&
          memcmp(&out,&sentinel,sizeof(out))==0);
    observation.generation=slot._generation;progress(31u);
}
static int negative_open(const char *path,dok_asset_status expected,uint32_t mode)
{
    NdsAssetFile file;dok_asset_reader reader;dok_asset_session out,before;
    dok_asset_status result=nds_asset_file_open(&file,path,&reader);
    if(result!=DOK_ASSET_OK) return result==expected;
    file.test_fault_mode=mode;file.test_fault_offset=0;
    memset(&out,0xa5,sizeof(out));before=out;
    result=dok_assets_open(&reader,workspace,&out);
    nds_asset_file_close(&file);
    return result==expected && memcmp(&out,&before,sizeof(out))==0;
}
static int negative_load(const char *path,dok_asset_status expected)
{
    NdsAssetFile file;dok_asset_reader reader;dok_asset_session small;
    dok_asset_entry entry;dok_asset_view out,before;
    int passed=nds_asset_file_open(&file,path,&reader)==DOK_ASSET_OK;
    if(!passed) return 0;
    passed=dok_assets_open(&reader,workspace,&small)==DOK_ASSET_OK &&
           dok_assets_entry_at(&small,1u,&entry)==DOK_ASSET_OK;
    memset(&out,0xa5,sizeof(out));before=out;
    if(passed) {
        passed=dok_assets_load(&small,entry.id,asset_pool,CANDIDATE_BYTES,&out)==expected;
        if(expected!=DOK_ASSET_OK) passed=passed && memcmp(&out,&before,sizeof(out))==0;
    }
    nds_asset_file_close(&file);return passed;
}
static void negative_checks(void)
{
    static const char *paths[7]={"fat:/dok/missing.ndp","fat:/dok/wrong.ndp",
        "fat:/dok/trunc.ndp","fat:/dok/badhash.ndp","fat:/dok/badschema.ndp",
        "fat:/dok/assets.ndp","fat:/dok/assets.ndp"};
    static const dok_asset_status expected[7]={DOK_ASSET_NOT_FOUND,DOK_ASSET_UNSUPPORTED_VERSION,
        DOK_ASSET_TRUNCATED,DOK_ASSET_HASH_MISMATCH,DOK_ASSET_MALFORMED_PAYLOAD,
        DOK_ASSET_SHORT_READ,DOK_ASSET_IO_ERROR};
    if(negative_load("fat:/dok/control.ndp",DOK_ASSET_OK)) observation.negative_pass_mask|=128u;
    for(uint32_t i=0;i<7u;++i) {
        int good=(i==3u || i==4u)?negative_load(paths[i],expected[i]):
            negative_open(paths[i],expected[i],i==5u?1u:(i==6u?2u:0u));
        if(good) observation.negative_pass_mask|=1u<<i;
        check(6u+i,good);progress(40u+i);
    }
    if((observation.negative_pass_mask&128u)==0u) observation.error_bits|=1u<<14;
}
static void final_cached_observation(void)
{
    dok_asset_entry entry;dok_asset_view view;
    for(uint32_t i=0;i<dok_assets_count(&session);++i) {
        if(dok_assets_entry_at(&session,i,&entry)!=DOK_ASSET_OK) break;
        if(entry.representation==DOK_ASSET_STATIC_INDEX8 && entry.language==1u) {
            if(dok_assets_load(&session,entry.id,asset_pool,CANDIDATE_BYTES,&view)==DOK_ASSET_OK) {
                observation.cached_base=address(view.data);observation.cached_id=entry.id;
                observation.cached_bytes=entry.bytes;
                DC_FlushRange(view.data,(entry.bytes+31u)&~31u);
                return;
            }
            break;
        }
    }
    observation.error_bits|=1u<<15;
}
static void screen(void)
{
    consoleSelect(&top);consoleClear();
    printf("N04 ASSETS / TOP\n\n%s\n\nentries %lu / raw %lu\ntypes %04lX\nROM has no game bank\nread-only FAT / DLDI\n\nPCM %lu samples\n%lu checked chunks\nGPU max %lu bytes\n",
        observation.status==1u?"PASS (EMULATOR)":observation.status==3u?"NO FAT MEDIA":
        observation.status==4u?"PACKAGE MISSING":"FAILED",
        (unsigned long)observation.entry_count,(unsigned long)observation.raw_count,
        (unsigned long)observation.type_mask,(unsigned long)observation.long_sample_count,
        (unsigned long)observation.long_chunks,(unsigned long)observation.largest_gpu_bytes);
    consoleSelect(&bottom);consoleClear();
    printf("N04 MEMORY / BOTTOM\n\npool %lu / used %lu\nworkspace %lu\nmax read %lu\n\nmask %04lX / errors %04lX\nnegative %02lX\nheap limit %08lX\nreserve %lu bytes\n\nReal DS check: N24\nNo physical FPS claim\n",
        (unsigned long)observation.asset_pool_bytes,(unsigned long)observation.asset_highwater,
        (unsigned long)observation.workspace_bytes,(unsigned long)observation.max_request,
        (unsigned long)observation.tests_pass_mask,(unsigned long)observation.error_bits,
        (unsigned long)observation.negative_pass_mask,(unsigned long)observation.heap_limit_final,
        (unsigned long)observation.reserve_bytes);
}
int main(void)
{
    dok_asset_reader reader;
    check(13u,memory_setup());progress(0u);
    videoSetMode(MODE_0_2D);videoSetModeSub(MODE_0_2D);
    vramSetBankA(VRAM_A_MAIN_BG);vramSetBankC(VRAM_C_SUB_BG);lcdMainOnTop();
    consoleInit(&top,0,BgType_Text4bpp,BgSize_T_256x256,31,0,true,true);
    consoleInit(&bottom,0,BgType_Text4bpp,BgSize_T_256x256,31,0,false,true);
    if(observation.error_bits!=0u) observation.status=2u;
    else if(!fatInitDefault()) observation.status=3u;
    else {
        observation.open_status=nds_asset_file_open(&package,"fat:/dok/assets.ndp",&reader);
        if(observation.open_status==DOK_ASSET_NOT_FOUND) observation.status=4u;
        else {
            if(observation.open_status==DOK_ASSET_OK)
                observation.open_status=dok_assets_open(&reader,workspace,&session);
            check(0u,observation.open_status==DOK_ASSET_OK);
            if(observation.open_status==DOK_ASSET_OK) {
                observation.entry_count=dok_assets_count(&session);observation.file_bytes=reader.file_bytes;
                typed_and_stream_checks();cache_checks();negative_checks();final_cached_observation();
            }
            observation.status=observation.error_bits==0u && observation.tests_pass_mask==NDS_ASSETS_PASS_MASK?1u:2u;
        }
        observation.heap_limit_final=address(getHeapLimit());
        observation.sdk_heap_available=observation.heap_limit_final-address(getHeapEnd());
        uint32_t overhead=address(getHeapEnd())-address(getHeapStart());
        check(13u,observation.heap_limit_final==DOK_NDS_ARM9_LIMIT-DOK_NDS_RESERVE_BYTES &&
              observation.sdk_heap_available>=DOK_NDS_SDK_HEAP_MIN && overhead>=allocation_bytes &&
              observation.static_bytes+overhead-allocation_bytes<=DOK_NDS_STATIC_CEILING);
        if(observation.error_bits!=0u) observation.status=2u;
    }
    observation.case_id=100u;publish();screen();
    while(1) {swiWaitForVBlank();++observation.frame;publish();}
}
