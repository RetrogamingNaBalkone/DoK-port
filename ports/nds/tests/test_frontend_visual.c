#include "dok/frontend_visual.h"
#include "dok/render_materials.h"
#include "../platform/assets/file_reader.h"
#include <stdio.h>
#include <string.h>

#ifndef VISUAL_BANK_LANGUAGE
#define VISUAL_BANK_LANGUAGE 1
#endif
static dok_render_world world;
static dok_render_workspace work;
static dok_asset_workspace io;
static dok_sprite_asset_bank bank,bank_before;
static dok_frame_store store,before;
static dok_texture_cache cache;
static dok_frontend_visual visual;
static unsigned char assets[DOK_NDS_ASSETS_BYTES],pool[524288],prefix[524288];
static unsigned char scratch[131072],raw_cache[65536],expected[2][32768];
static unsigned checks;
#define CHECK(e) do{++checks;if(!(e)){fprintf(stderr,"frontend_visual:%d: %s\n",__LINE__,#e);return 1;}}while(0)
static int read_expected(const char *name,unsigned char *out)
{
    FILE *f=fopen(name,"rb");if(!f)return 0;
    size_t bytes=fread(out,1,32768,f);int extra=fgetc(f);fclose(f);
    return bytes==32768u&&extra==EOF;
}
static int unchanged(void)
{
    return !memcmp(&store,&before,sizeof(store))&&!memcmp(pool,prefix,before.bytes);
}
int main(int argc,char **argv)
{
    CHECK(argc==4);CHECK(read_expected(argv[2],expected[0]));CHECK(read_expected(argv[3],expected[1]));
    unsigned differences=0;
    for(unsigned i=0;i<32768u;++i)if(expected[0][i]!=expected[1][i]){
        CHECK(i==17819u||i==17820u||i==17821u);++differences;
    }
    CHECK(differences==3u);
    NdsAssetFile file;dok_asset_reader reader;dok_asset_session session;
    CHECK(nds_asset_file_open(&file,argv[1],&reader)==DOK_ASSET_OK);
    CHECK(dok_assets_open(&reader,&io,&session)==DOK_ASSET_OK);
    CHECK(dok_frontend_visual_init(&visual,&session)==DOK_STATUS_OK);
    CHECK(visual.entries[0].id==0x42441405u&&visual.entries[1].id==0x82441405u);
    CHECK(dok_render_world_load(&session,1,1,&world,&work,assets,sizeof(assets))==DOK_STATUS_OK);
    if(VISUAL_BANK_LANGUAGE==2){
        dok_asset_view view;uint32_t source_off=(world.asset_bytes+31u)&~31u;
        CHECK(dok_assets_load(&session,(2u<<30)|(18u<<18)|(DOK_ASSET_LEVEL_CP866<<10),
            assets+source_off,DOK_RENDER_SOURCE_BYTES,&view)==DOK_ASSET_OK);
        CHECK(dok_level_parse(&view,&work.world.level,&world.base)==DOK_WORLD_OK);
        CHECK(dok_presentation_bind(&world.effective,&world.base,&session,&work.presentation,
            &world.presentation)==DOK_BALANCE_OK);
        world.asset_bytes=source_off+view.entry.bytes;
    }
    uint32_t off=(world.asset_bytes+31u)&~31u;
    CHECK(dok_sprite_assets_load(&world.presentation,&bank,assets+off,sizeof(assets)-off)==DOK_STATUS_OK);
    bank_before=bank;
    const dok_asset_entry *source=dok_sprite_asset_image(&bank,visual.entries[VISUAL_BANK_LANGUAGE-1].id);
    CHECK(source&&source->id==visual.entries[VISUAL_BANK_LANGUAGE-1].id);
    CHECK(!dok_sprite_asset_image(&bank,visual.entries[2-VISUAL_BANK_LANGUAGE].id));
    CHECK(dok_frame_store_load(&store,&bank,&session,pool,sizeof(pool),scratch,sizeof(scratch))==DOK_STATUS_OK);
    before=store;memcpy(prefix,pool,store.bytes);
    CHECK(dok_frontend_visual_append_store(&visual,&store,&session,pool,sizeof(pool),scratch,sizeof(scratch))==DOK_STATUS_OK);
    CHECK(store.count==before.count+1u&&store.decoded_bytes==before.decoded_bytes+32768u);
    CHECK(store.bytes>before.bytes&&!memcmp(pool,prefix,before.bytes));
    CHECK(!memcmp(store.records,before.records,before.count*sizeof(store.records[0])));
    CHECK(!memcmp(&bank,&bank_before,sizeof(bank)));
    uint32_t packed_bytes=store.bytes,frame_count=store.count,reads=file.calls;
    before=store;memcpy(prefix,pool,store.bytes);
    CHECK(dok_frontend_visual_append_store(&visual,&store,&session,pool,sizeof(pool),scratch,sizeof(scratch))==DOK_STATUS_OK);
    CHECK(unchanged()); /* idempotent; no duplicate resident frame */
    dok_texture_cache_init(&cache,raw_cache,sizeof(raw_cache));
    for(unsigned pass=0;pass<8u;++pass)for(unsigned language=1;language<=2u;++language){
        const dok_asset_entry *resolved=dok_frontend_visual_resolve(&visual,source,(uint8_t)language);
        CHECK(resolved==&visual.entries[language-1u]);
        CHECK(resolved->id==((language<<30)|(145u<<18)|(5u<<10)|5u));
        CHECK(dok_texture_cache_begin(&cache)==DOK_STATUS_OK);
        reads=file.calls;
        CHECK(dok_frame_store_require(&store,&cache,resolved)==DOK_STATUS_OK);
        CHECK(file.calls==reads);
        const unsigned char *pixels=dok_texture_cache_pixels(&cache,resolved->id);
        CHECK(pixels!=NULL);
        for(unsigned i=0;i<32768u;++i)CHECK(pixels[i]==expected[language-1u][i]);
        CHECK(!memcmp(&bank,&bank_before,sizeof(bank)));
    }
    unsigned decodes=store.decodes;
    for(unsigned i=0;i<bank.image_count;++i){
        if(bank.images[i].id==source->id)continue;
        for(unsigned language=1;language<=2u;++language)
            CHECK(dok_frontend_visual_resolve(&visual,&bank.images[i],(uint8_t)language)==&bank.images[i]);
    }
    CHECK(dok_frontend_visual_resolve(&visual,source,0)==source);
    CHECK(dok_frontend_visual_resolve(&visual,source,3)==source);
    CHECK(dok_frontend_visual_resolve(NULL,source,1)==source);
    CHECK(dok_frontend_visual_resolve(&visual,NULL,1)==NULL);
    /* Failure gates operate on a fresh, fully valid original store. */
    CHECK(dok_frame_store_load(&store,&bank,&session,pool,sizeof(pool),scratch,sizeof(scratch))==DOK_STATUS_OK);
    before=store;memcpy(prefix,pool,store.bytes);
    CHECK(dok_frontend_visual_append_store(&visual,&store,&session,pool,store.bytes,scratch,sizeof(scratch))==DOK_STATUS_NO_SPACE);
    CHECK(unchanged());
    CHECK(dok_frontend_visual_append_store(&visual,&store,&session,pool,sizeof(pool),scratch,65535)==DOK_STATUS_NO_SPACE);
    CHECK(unchanged());
    CHECK(dok_frontend_visual_append_store(&visual,&store,&session,pool+1,sizeof(pool)-1,scratch,sizeof(scratch))==DOK_STATUS_INVALID_ARGUMENT);
    CHECK(unchanged());
    CHECK(dok_frontend_visual_append_store(&visual,&store,&session,pool,sizeof(pool),pool,65536)==DOK_STATUS_INVALID_ARGUMENT);
    CHECK(unchanged());
    file.test_fault_offset=visual.entries[2-VISUAL_BANK_LANGUAGE].offset;file.test_fault_mode=2;
    CHECK(dok_frontend_visual_append_store(&visual,&store,&session,pool,sizeof(pool),scratch,sizeof(scratch))==DOK_STATUS_INVALID_STATE);
    CHECK(unchanged());file.test_fault_mode=0;
    store.count=257;before=store;
    CHECK(dok_frontend_visual_append_store(&visual,&store,&session,pool,sizeof(pool),scratch,sizeof(scratch))==DOK_STATUS_INVALID_ARGUMENT);
    CHECK(unchanged());store.count=bank.image_count;
    store.records[0].offset=store.bytes+1u;before=store;
    CHECK(dok_frontend_visual_append_store(&visual,&store,&session,pool,sizeof(pool),scratch,sizeof(scratch))==DOK_STATUS_INVALID_STATE);
    CHECK(unchanged());
    CHECK(dok_frame_store_load(&store,&bank,&session,pool,sizeof(pool),scratch,sizeof(scratch))==DOK_STATUS_OK);
    store.decoded_bytes^=1u;before=store;memcpy(prefix,pool,store.bytes);
    CHECK(dok_frontend_visual_append_store(&visual,&store,&session,pool,sizeof(pool),scratch,sizeof(scratch))==DOK_STATUS_INVALID_STATE);
    CHECK(unchanged());
    store=(dok_frame_store){0};store.pool=pool;store.count=256;store.bytes=1;store.decoded_bytes=256;
    for(unsigned i=0;i<256u;++i)store.records[i]=(dok_frame_record){0x30000000u+i,0,1,1};
    before=store;memcpy(prefix,pool,store.bytes);
    CHECK(dok_frontend_visual_append_store(&visual,&store,&session,pool,sizeof(pool),scratch,sizeof(scratch))==DOK_STATUS_NO_SPACE);
    CHECK(unchanged());
    store.records[1].id=store.records[0].id;before=store;
    CHECK(dok_frontend_visual_append_store(&visual,&store,&session,pool,sizeof(pool),scratch,sizeof(scratch))==DOK_STATUS_INVALID_STATE);
    CHECK(unchanged());
    /* A second-frame failure must not publish the first append either. */
    store=(dok_frame_store){0};store.pool=pool;before=store;
    file.test_fault_offset=visual.entries[1].offset;file.test_fault_mode=2;
    CHECK(dok_frontend_visual_append_store(&visual,&store,&session,pool,sizeof(pool),scratch,sizeof(scratch))==DOK_STATUS_INVALID_STATE);
    CHECK(unchanged());file.test_fault_mode=0;
    CHECK(dok_frontend_visual_append_store(&visual,&store,&session,pool,sizeof(pool),scratch,sizeof(scratch))==DOK_STATUS_OK);
    CHECK(store.count==2&&store.decoded_bytes==65536u);
    dok_frontend_visual preserved=visual;dok_asset_session invalid={0};
    CHECK(dok_frontend_visual_init(&visual,&invalid)==DOK_STATUS_INVALID_STATE);
    CHECK(!memcmp(&visual,&preserved,sizeof(visual)));
    CHECK(!memcmp(&bank,&bank_before,sizeof(bank)));
    nds_asset_file_close(&file);
    printf("{\"passed\":true,\"checks\":%u,\"bank_language\":%u,\"frames\":%u,\"packed_bytes\":%u,\"decoded\":%u,\"source_different_pixels\":%u}\n",
        checks,VISUAL_BANK_LANGUAGE,frame_count,packed_bytes,decodes,differences);
    return 0;
}
