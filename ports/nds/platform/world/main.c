/* Test observation only: original-backed initial data, not a playable engine. */
#include <nds.h>
#include <fat.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dok/balance.h"
#include "dok/sha256.h"
#include "file_reader.h"
#include "world_export.h"
#include "world_probe.h"

#define SLOT_BYTES 65536u
#define EXPORT_OFFSET (3u*SLOT_BYTES)
#define EXPORT_CAPACITY 524288u
typedef struct ProbeWorld {
    dok_level_definition base, effective, localized;
    dok_initial_world initial;
    dok_level_presentation presentation;
    dok_balance_diff diff;
} ProbeWorld;
typedef union ProbeScratch {
    dok_world_workspace world;
    dok_level_presentation presentation;
} ProbeScratch;
_Static_assert(sizeof(ProbeWorld)<=DOK_NDS_WORLD_BYTES,"world storage budget");
_Static_assert(sizeof(ProbeScratch)<=DOK_NDS_SCRATCH_BYTES,"scratch budget");
_Static_assert(EXPORT_OFFSET+EXPORT_CAPACITY<=DOK_NDS_ASSETS_BYTES,"asset staging budget");
extern unsigned char __end__,__eheap_end;
_Alignas(32) volatile NdsWorldProbe g_nds_world_probe={
    .magic=NDS_WORLD_MAGIC,.abi_version=NDS_WORLD_ABI,.size_bytes=sizeof(NdsWorldProbe)
};
static NdsWorldProbe obs;
static PrintConsole top,bottom;
static NdsAssetFile package;
static dok_asset_session session;
static dok_asset_workspace *io;
static ProbeWorld *world;
static ProbeScratch *scratch;
static unsigned char *assets;
static uint32_t export_bytes;

static uint32_t addr(const void *p) { return (uint32_t)(uintptr_t)p; }
static void hash_bytes(const void *bytes,uint32_t count,unsigned char out[32])
{ dok_sha256 hash;dok_sha256_begin(&hash);dok_sha256_update(&hash,bytes,count);dok_sha256_end(&hash,out); }
static void check(uint32_t bit,int ok)
{ if(ok) obs.tests_pass_mask|=1u<<bit; else obs.error_bits|=1u<<bit; }
static void publish(void)
{
    uint32_t sequence;
    ++g_nds_world_probe.sequence;
    DC_FlushRange((const void *)&g_nds_world_probe.sequence,32);
    sequence=g_nds_world_probe.sequence;
    obs.magic=NDS_WORLD_MAGIC;obs.abi_version=NDS_WORLD_ABI;
    obs.size_bytes=sizeof(obs);obs.sequence=sequence;
    obs.read_calls=package.calls;obs.read_bytes=package.bytes_read;obs.max_request=package.max_request;
    memcpy((void *)&g_nds_world_probe,&obs,sizeof(obs));
    DC_FlushRange((const void *)&g_nds_world_probe,sizeof(g_nds_world_probe));
    ++g_nds_world_probe.sequence;
    DC_FlushRange((const void *)&g_nds_world_probe.sequence,32);
}
static void tick(void) { swiWaitForVBlank();++obs.frame;publish(); }
static int setup(void)
{
    uint32_t before=addr(getHeapLimit()),limit=addr(&__eheap_end);
    obs.heap_limit_before=before;obs.static_bytes=addr(&__end__)-DOK_NDS_MAIN_BASE;
    obs.reserve_bytes=DOK_NDS_RESERVE_BYTES;
    if(isDSiMode() || limit!=DOK_NDS_ARM9_LIMIT || before<limit ||
       obs.static_bytes>DOK_NDS_STATIC_CEILING) return 0;
    if(before!=limit && reduceHeapSize(before-limit)!=0) return 0;
    if(reduceHeapSize(DOK_NDS_RESERVE_BYTES)!=0) return 0;
    world=malloc(DOK_NDS_WORLD_BYTES);scratch=malloc(DOK_NDS_SCRATCH_BYTES);
    io=malloc(DOK_NDS_IO_BYTES);assets=malloc(DOK_NDS_ASSETS_BYTES);
    if(!world || !scratch || !io || !assets) return 0;
    obs.world_base=addr(world);obs.world_bytes=sizeof(*world);obs.scratch_bytes=sizeof(*scratch);
    obs.asset_pool_bytes=DOK_NDS_ASSETS_BYTES;obs.io_bytes=sizeof(*io);
    obs.allocated_slabs=DOK_NDS_WORLD_BYTES+DOK_NDS_SCRATCH_BYTES+
        DOK_NDS_IO_BYTES+DOK_NDS_ASSETS_BYTES;
    obs.heap_limit_final=addr(getHeapLimit());
    obs.sdk_heap_available=obs.heap_limit_final-addr(getHeapEnd());
    return obs.heap_limit_final==DOK_NDS_ARM9_LIMIT-DOK_NDS_RESERVE_BYTES &&
        obs.sdk_heap_available>=DOK_NDS_SDK_HEAP_MIN;
}
static dok_asset_id level_id(uint32_t index,uint32_t language,uint32_t representation)
{ return language<<30 | index<<18 | representation<<10; }
static int load_level(uint32_t index,uint32_t language,uint32_t slot,dok_level_definition *out)
{
    dok_asset_view view;
    return dok_assets_load(&session,level_id(index,language,DOK_ASSET_LEVEL_CP866),
        assets+slot*SLOT_BYTES,SLOT_BYTES,&view)==DOK_ASSET_OK &&
        dok_level_parse(&view,&scratch->world.level,out)==DOK_WORLD_OK;
}
static int emit(void *context,const char *data,uint32_t count)
{
    (void)context;
    if(count>EXPORT_CAPACITY-export_bytes) return 0;
    memcpy(assets+EXPORT_OFFSET+export_bytes,data,count);export_bytes+=count;return 1;
}
static void negative_checks(const dok_balance_profile *profile)
{
    unsigned char bad_archive[32],old_byte,old_model[32],after[32];
    dok_balance_diff diff=world->diff;
    dok_balance_profile invalid=*profile;
    memcpy(bad_archive,session._source_sha256[0],32);bad_archive[0]^=1u;
    hash_bytes(&world->effective,sizeof(world->effective),old_model);
    if(dok_balance_apply(profile,bad_archive,&world->base,&scratch->world.level,
       &world->effective,&world->diff)!=DOK_BALANCE_OK) obs.negative_pass_mask|=1u;
    old_byte=assets[0];assets[0]^=1u;
    if(dok_balance_apply(profile,session._source_sha256[0],&world->base,&scratch->world.level,
       &world->effective,&world->diff)!=DOK_BALANCE_OK) obs.negative_pass_mask|=2u;
    assets[0]=old_byte;
    world->base.settings[0]^=1;
    if(dok_balance_apply(profile,session._source_sha256[0],&world->base,&scratch->world.level,
       &world->effective,&world->diff)!=DOK_BALANCE_OK) obs.negative_pass_mask|=4u;
    world->base.settings[0]^=1;
    invalid.revision++;
    if(dok_balance_apply(&invalid,session._source_sha256[0],&world->base,&scratch->world.level,
       &world->effective,&world->diff)!=DOK_BALANCE_OK) obs.negative_pass_mask|=8u;
    if(dok_initial_world_build(&world->localized,1,&scratch->world.world,
       &world->initial)!=DOK_WORLD_OK) obs.negative_pass_mask|=16u;
    if(dok_balance_apply(profile,session._source_sha256[0],&world->localized,&scratch->world.level,
       &world->effective,&world->diff)!=DOK_BALANCE_OK) obs.negative_pass_mask|=32u;
    hash_bytes(&world->effective,sizeof(world->effective),after);
    check(4u,obs.negative_pass_mask==63u && !memcmp(old_model,after,32) &&
        !memcmp(&diff,&world->diff,sizeof(diff)));
}
static int prepare(uint32_t level)
{
    dok_asset_view maps;
    dok_balance_profile profile=dok_balance_accepted_profile();
    uint32_t index=18u+level;
    if(!load_level(index,1,0,&world->base)) return 0;
    if(dok_balance_apply(&profile,session._source_sha256[0],&world->base,
       &scratch->world.level,&world->effective,&world->diff)!=DOK_BALANCE_OK) return 0;
    if(dok_initial_world_build(&world->effective,level+1,&scratch->world.world,
       &world->initial)!=DOK_WORLD_OK) return 0;
    if(dok_assets_load(&session,level_id(index,1,DOK_ASSET_LEVEL_MAPS),assets+2u*SLOT_BYTES,
       SLOT_BYTES,&maps)!=DOK_ASSET_OK ||
       dok_level_validate_maps(&world->base,&maps)!=DOK_WORLD_OK) return 0;
    if(!load_level(index,2,1,&world->localized)) return 0;
    if(level==0u) negative_checks(&profile);
    obs.changes=world->diff.count;obs.retained_assertions=world->diff.retained_assertions;
    obs.boyars=world->diff.boyar_definitions;obs.mice=world->diff.mouse_definitions;
    obs.actor_placements=world->initial.actor_placements;obs.placed_actors=world->initial.placed_actors;
    return 1;
}
static void screen(void)
{
    consoleSelect(&top);consoleClear();
    printf("N05 RU WORLD / TOP\n\n%s\nLEVEL%lu / language %lu\ncase %lu of 6\n\nchanges %lu\nprofile defs %lu / %lu\nplacements %lu\n\nOne RU world, RU/EN text\nRead-only FAT / DLDI\n",
        obs.status==3u?"PASS (EMULATOR)":obs.status==1u?"EXPORT READY":"FAILED",
        (unsigned long)obs.level,(unsigned long)obs.language,(unsigned long)obs.case_id,
        (unsigned long)obs.changes,(unsigned long)obs.boyars,(unsigned long)obs.mice,
        (unsigned long)obs.actor_placements);
    consoleSelect(&bottom);consoleClear();
    printf("N05 DATA / BOTTOM\n\nworld %lu / 393216\nscratch %lu / 131072\nJSON %lu bytes\n\nmask %02lX errors %02lX\nnegative %02lX\nreserve %lu bytes\n\nA: inspect next case\nReal DS check: N24\nNo game renderer yet\n",
        (unsigned long)obs.world_bytes,(unsigned long)obs.scratch_bytes,(unsigned long)obs.json_bytes,
        (unsigned long)obs.tests_pass_mask,(unsigned long)obs.error_bits,
        (unsigned long)obs.negative_pass_mask,(unsigned long)obs.reserve_bytes);
}
int main(void)
{
    dok_asset_reader reader;
    check(0u,setup());
    videoSetMode(MODE_0_2D);videoSetModeSub(MODE_0_2D);
    vramSetBankA(VRAM_A_MAIN_BG);vramSetBankC(VRAM_C_SUB_BG);lcdMainOnTop();
    consoleInit(&top,0,BgType_Text4bpp,BgSize_T_256x256,31,0,true,true);
    consoleInit(&bottom,0,BgType_Text4bpp,BgSize_T_256x256,31,0,false,true);
    if(obs.error_bits) goto done;
    check(1u,fatInitDefault() && nds_asset_file_open(&package,"fat:/dok/assets.ndp",&reader)==DOK_ASSET_OK &&
        dok_assets_open(&reader,io,&session)==DOK_ASSET_OK);
    if(obs.error_bits) goto done;
    obs.entry_count=dok_assets_count(&session);
    for(uint32_t level=0;level<3u;++level) {
        check(2u,prepare(level));if(obs.error_bits) goto done;
        obs.level=level+1;
        for(uint32_t language=1;language<=2u;++language) {
            const dok_level_definition *localized=language==1u?&world->base:&world->localized;
            check(3u,dok_presentation_bind(&world->effective,localized,&session,
                &scratch->presentation,&world->presentation)==DOK_BALANCE_OK);
            if(obs.error_bits) goto done;
            export_bytes=0;
            check(5u,dok_test_world_export(&world->base,&world->effective,&world->initial,
                &world->presentation,emit,NULL));
            if(obs.error_bits) goto done;
            obs.json_base=addr(assets+EXPORT_OFFSET);obs.json_bytes=export_bytes;
            obs.language=language;++obs.case_id;obs.case_count=6u;obs.status=1u;
            DC_FlushRange(assets+EXPORT_OFFSET,(export_bytes+31u)&~31u);
            publish();screen();
            do {tick();scanKeys();} while(!(keysDown()&KEY_A));
            /* Require release so a held key cannot skip the next case. */
            do {tick();scanKeys();} while(keysHeld()&KEY_A);
        }
    }
    obs.heap_limit_final=addr(getHeapLimit());
    obs.sdk_heap_available=obs.heap_limit_final-addr(getHeapEnd());
    {
        /* Include any alignment gap between static end and the first heap
         * allocation in the same conservative static/startup envelope. */
        uint32_t used=addr(getHeapEnd())-addr(&__end__);
        if(used<obs.allocated_slabs) obs.error_bits|=1u<<6;
        else obs.startup_overhead=used-obs.allocated_slabs;
    }
    check(6u,obs.heap_limit_final==DOK_NDS_ARM9_LIMIT-DOK_NDS_RESERVE_BYTES &&
        obs.sdk_heap_available>=DOK_NDS_SDK_HEAP_MIN &&
        obs.static_bytes+obs.startup_overhead<=DOK_NDS_STATIC_CEILING);
done:
    obs.status=obs.error_bits?2u:3u;publish();screen();
    while(1) tick();
}
