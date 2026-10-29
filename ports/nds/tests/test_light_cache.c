#include "dok/light_cache.h"
#include "dok/render_materials.h"
#include "dok/sprite_assets.h"
#include "../platform/assets/file_reader.h"
#include <stdio.h>
#include <string.h>
static dok_render_scene a,b;
static dok_light_workspace scratch;
static dok_light_cache cache;
static dok_render_world world;
static dok_render_workspace loader;
static dok_asset_workspace io;
static dok_sprite_asset_bank bank;
static dok_sprite_list list;
static uint16_t slots[DOK_SPRITE_LIMIT];
static unsigned char assets[DOK_NDS_ASSETS_BYTES];
static unsigned checks,comparisons;
#define CHECK(c) do{++checks;if(!(c)){fprintf(stderr,"light cache:%d\n",__LINE__);return 0;}}while(0)
static void copy_scene(void)
{
    b=a;b.frame.cells=b.cells;
    for(uint32_t i=0;i<b.frame.cell_count;++i)b.cells[i].quads=b.quads+(a.cells[i].quads-a.quads);
}
static int compare(const dok_light_counts *x,const dok_light_counts *y)
{
    CHECK(!memcmp(x,y,sizeof(*x)));CHECK(a.frame.generation==b.frame.generation);
    CHECK(a.generation==b.generation && a.leased==b.leased && a.world_epoch==b.world_epoch);
    CHECK(a.frame.cell_count==b.frame.cell_count && a.counts.published_quads==b.counts.published_quads);
    CHECK(!memcmp(a.quads,b.quads,a.counts.published_quads*sizeof(*a.quads)));
    for(uint32_t i=0;i<a.frame.cell_count;++i) {
        const dok_nds_render_cell *aa=&a.cells[i],*bb=&b.cells[i];
        CHECK(aa->origin_x==bb->origin_x && aa->origin_y==bb->origin_y && aa->origin_z==bb->origin_z && aa->quad_count==bb->quad_count);
        CHECK(aa->quads-a.quads==bb->quads-b.quads);
    }
    ++comparisons;return 1;
}
static void one(uint32_t count,int32_t origin)
{
    dok_render_scene_init(&a);a.leased=1;a.world_epoch=1;a.generation=7;
    a.frame=(dok_nds_render_frame){{origin,-300,0,0,32768},a.cells,1,7};a.counts.published_quads=count;
    for(uint32_t i=0;i<count;++i) {
        dok_nds_render_quad *q=&a.quads[i];memset(q,0,sizeof(*q));q->texture_slot=0;q->color_rgb555=32767;q->source_surface_id=0xc1000000u+i;
        q->vertex[0]=(dok_nds_render_vertex){-480,-960,3000,1,2};
        q->vertex[1]=(dok_nds_render_vertex){480,-960,3000,15*65536+3,4};
        q->vertex[2]=(dok_nds_render_vertex){480,0,3000,15*65536+5,15*65536+6};
        q->vertex[3]=(dok_nds_render_vertex){-480,0,3000,7,15*65536+8};
    }
    a.cells[0]=(dok_nds_render_cell){origin,0,0,a.quads,count};
}
static int synthetic(void)
{
    dok_light_counts x,y;dok_light_cache_reset(&cache);
    for(uint32_t turn=0;turn<6u;++turn) {
        one(1,0);
        if(turn>=2u)a.quads[0].texture_slot=UINT16_MAX;
        if(turn>=4u)a.quads[0].color_rgb555=31;
        copy_scene();CHECK(dok_light_scene_apply(&b,1920,&scratch,&y)==DOK_RENDER_SCENE_OK);
        CHECK(dok_light_cache_apply(&cache,&a,1920,&scratch,&x)==DOK_RENDER_SCENE_OK);
        CHECK(cache.last_hit==(turn%2u));CHECK(compare(&x,&y));
    }
    one(385,0);copy_scene();CHECK(dok_light_scene_apply(&b,1920,&scratch,&y)==DOK_RENDER_SCENE_OK);
    CHECK(dok_light_cache_apply(&cache,&a,1920,&scratch,&x)==DOK_RENDER_SCENE_OK && !cache.last_hit && !cache.valid);
    CHECK(compare(&x,&y));
    one(1,32768);copy_scene();CHECK(dok_light_scene_apply(&b,1920,&scratch,&y)==DOK_RENDER_SCENE_OK);
    CHECK(dok_light_cache_apply(&cache,&a,1920,&scratch,&x)==DOK_RENDER_SCENE_OK && !cache.valid && !cache.last_hit);
    CHECK(compare(&x,&y));
    one(1,0);a.quads[0].vertex[0].x=4096;copy_scene();memset(&x,0xa5,sizeof(x));y=x;
    CHECK(dok_light_cache_apply(&cache,&a,1920,&scratch,&x)==DOK_RENDER_SCENE_ARGUMENT);
    CHECK(!memcmp(&x,&y,sizeof(x)) && !memcmp(a.quads,b.quads,sizeof(a.quads)));
    return 1;
}
static int actual(const char *path)
{
    NdsAssetFile file;dok_asset_reader reader;dok_asset_session session;
    static const int32_t starts[3][2]={{15120,22800},{20880,3120},{17520,28560}};
    CHECK(nds_asset_file_open(&file,path,&reader)==DOK_ASSET_OK);CHECK(dok_assets_open(&reader,&io,&session)==DOK_ASSET_OK);
    for(uint32_t level=1;level<=3u;++level) {
        CHECK(dok_render_world_load(&session,level,level,&world,&loader,assets,sizeof(assets))==DOK_STATUS_OK);
        uint32_t offset=(world.asset_bytes+31u)&~31u;
        CHECK(dok_sprite_assets_load(&world.presentation,&bank,assets+offset,sizeof(assets)-offset)==DOK_STATUS_OK);
        dok_light_cache_reset(&cache);dok_render_scene_init(&a);
        for(uint32_t heading=0;heading<360u;heading+=45u)for(uint32_t repeat=0;repeat<2u;++repeat) {
            const dok_nds_render_frame *frame;const dok_sprite_list *published;
            dok_nds_render_camera camera={starts[level-1u][0],-300,starts[level-1u][1],0,0};dok_light_counts x,y;
            CHECK(dok_render_trig(heading,&camera.sin_q15,&camera.cos_q15));
            CHECK(dok_render_scene_prepare(&a,&world.initial,world.bindings,&camera,heading,&frame)==DOK_RENDER_SCENE_OK);
            dok_sprite_snapshot snapshot={world.initial.epoch,a.generation,bank.bindings,&world.initial};
            CHECK(dok_sprites_gather(&snapshot,&camera,heading,&list,&published)==DOK_STATUS_OK);
            CHECK(dok_sprites_append(&a,published,slots,a.generation)==DOK_RENDER_SCENE_OK);copy_scene();
            CHECK(dok_light_scene_apply(&b,1920,&scratch,&y)==DOK_RENDER_SCENE_OK);
            CHECK(dok_light_cache_apply(&cache,&a,1920,&scratch,&x)==DOK_RENDER_SCENE_OK);CHECK(compare(&x,&y));
            if(repeat && cache.valid)CHECK(cache.last_hit);
            CHECK(dok_render_scene_release(&a,frame->generation)==DOK_RENDER_SCENE_OK);
        }
    }
    nds_asset_file_close(&file);return 1;
}
int main(int argc,char **argv)
{
    if(argc!=2 || !synthetic() || !actual(argv[1]))return 1;
    printf("{\"checks\":%u,\"failures\":0,\"exact_packet_comparisons\":%u,\"cache_bytes\":%lu}\n",checks,comparisons,(unsigned long)sizeof(cache));return 0;
}
