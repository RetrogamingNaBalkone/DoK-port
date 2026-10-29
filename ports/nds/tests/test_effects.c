#include "dok/effects.h"
#include "dok/effect_cache.h"
#include <stdio.h>
#include <string.h>
static unsigned checks,failures;
#define CHECK(c) do{++checks;if(!(c)){++failures;fprintf(stderr,"effects:%d\n",__LINE__);}}while(0)
static dok_render_scene scene,old;
static dok_light_workspace scratch;
static unsigned char rgb6[768],light[4096],source[256],pixels[256],cache_pool[512];
static uint16_t palette[4096],before[4096];
static dok_effect_cache cache;
static void scene_one(void)
{
    dok_render_scene_init(&scene);scene.leased=1;scene.world_epoch=1;scene.generation=1;
    scene.frame=(dok_nds_render_frame){{0,-300,0,0,32768},scene.cells,1,1};
    scene.counts.published_quads=1;
    scene.quads[0]=(dok_nds_render_quad){.texture_slot=0,.color_rgb555=32767,.source_surface_id=17};
    scene.quads[0].vertex[0]=(dok_nds_render_vertex){-480,-960,3000,0,0};
    scene.quads[0].vertex[1]=(dok_nds_render_vertex){480,-960,3000,15*65536,0};
    scene.quads[0].vertex[2]=(dok_nds_render_vertex){480,0,3000,15*65536,15*65536};
    scene.quads[0].vertex[3]=(dok_nds_render_vertex){-480,0,3000,0,15*65536};
    scene.cells[0]=(dok_nds_render_cell){0,0,0,scene.quads,1};
}
int main(void)
{
    uint32_t i,cancel,arm;dok_water_state water={18,17,0};dok_light_counts counts;
    for(i=0;i<4096u;++i)light[i]=(unsigned char)(i&255u);
    for(i=0;i<768u;++i)rgb6[i]=(unsigned char)(i%64u);
    CHECK(dok_light_palette(rgb6,light,99,palette)==DOK_STATUS_OK);
    CHECK(palette[1]==2081u); /* VGA6(3,4,5) at99% ->RGB5(1,1,2). */
    memcpy(before,palette,sizeof(before));rgb6[767]=64;
    CHECK(dok_light_palette(rgb6,light,99,palette)==DOK_STATUS_INVALID_ARGUMENT && !memcmp(before,palette,sizeof(before)));
    rgb6[767]=63;
    CHECK(dok_water_rearm(&water,&arm)==DOK_STATUS_OK && arm && !water.counter && water.displayed_phase==17);
    for(i=0;i<18u;++i)CHECK(dok_water_step(&water,&cancel)==DOK_STATUS_OK && !cancel && water.displayed_phase==(int16_t)i && water.counter==(int16_t)(i+1u));
    CHECK(dok_water_step(&water,&cancel)==DOK_STATUS_OK && cancel && water.displayed_phase==17);
    for(i=0;i<256u;++i)source[i]=(unsigned char)i;
    light[7]=0;
    CHECK(dok_effects_pixels(source,16,16,16,16,0,18,0,light,1,pixels,sizeof(pixels))==DOK_STATUS_OK);
    CHECK(pixels[7]==0 && pixels[8]==8 && pixels[0]==0);
    dok_effect_cache_init(&cache,cache_pool,256);
    CHECK(dok_effect_cache_begin(&cache)==DOK_STATUS_OK);
    CHECK(dok_effect_cache_require(&cache,1,source,16,16,16,16,0,0,light,1)==DOK_STATUS_OK);
    CHECK(dok_effect_cache_require(&cache,2,source,16,16,16,16,0,0,light,1)==DOK_STATUS_NO_SPACE);
    CHECK(dok_effect_cache_pixels(&cache,1)[7]==0);
    CHECK(dok_effect_cache_begin(&cache)==DOK_STATUS_OK);
    CHECK(dok_effect_cache_require(&cache,2,source,16,16,16,16,0,0,light,1)==DOK_STATUS_OK && cache.evictions==1);
    scene_one();CHECK(dok_light_scene_apply(&scene,1920,&scratch,&counts)==DOK_RENDER_SCENE_OK);
    CHECK(counts.published_quads==1 && counts.rows[9]==1 && scene.quads[0].reserved==9);
    scene_one();scene.quads[0].vertex[1].x=4096;old=scene;
    memset(&counts,0xa5,sizeof(counts));dok_light_counts saved=counts;
    CHECK(dok_light_scene_apply(&scene,1920,&scratch,&counts)==DOK_RENDER_SCENE_ARGUMENT && !memcmp(&scene,&old,sizeof(scene)) && !memcmp(&counts,&saved,sizeof(counts)));
    scene_one();
    scene.quads[0].vertex[0]=(dok_nds_render_vertex){-480,0,1000,0,0};
    scene.quads[0].vertex[1]=(dok_nds_render_vertex){480,0,1000,15*65536,0};
    scene.quads[0].vertex[2]=(dok_nds_render_vertex){480,0,4000,15*65536,15*65536};
    scene.quads[0].vertex[3]=(dok_nds_render_vertex){-480,0,4000,0,15*65536};
    CHECK(dok_light_scene_apply(&scene,1920,&scratch,&counts)==DOK_RENDER_SCENE_OK);
    CHECK(counts.published_quads>=16 && counts.rows[0] && counts.rows[15]);
    printf("{\"checks\":%u,\"failures\":%u,\"floor_pieces\":%lu}\n",checks,failures,(unsigned long)counts.published_quads);
    return failures?1:0;
}
