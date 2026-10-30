/* Execute production map runtime against a deterministic RAM/DMA device.
 * This verifies caching/lifetimes/scheduling, rather than replacing the native
 * x86 pixel oracle used by the emulator acceptance suite. */
#include "../platform/map/runtime.h"
#include "map_runtime_stub/nds.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static dok_render_world world;
static NdsGameplay game;
static NdsMapRuntime map;
static unsigned char planes[12288],post[4096],pixels[49152],vram[49152],font[2048];
static unsigned char saved_planes[12288],saved_pre[4096],saved_post[4096],saved_pixels[49152];
static uint32_t clock_ticks=100,dmas,checks,frame;
uint16_t test_sub_palette[256];
uint32_t cpuGetTiming(void){return clock_ticks++;}
void DC_FlushRange(void *p,size_t n){(void)p;(void)n;}
void dmaCopy(const void *a,void *b,size_t n){memcpy(b,a,n);++dmas;}
dok_status dok_world_clock_pause(dok_world_clock *c){c->_paused=1;return DOK_STATUS_OK;}
dok_status dok_world_clock_resume(dok_world_clock *c){c->_paused=0;return DOK_STATUS_OK;}
dok_status dok_controls_initialize(dok_control_latch *c){memset(c,0,sizeof(*c));return DOK_STATUS_OK;}
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void update(uint32_t language){
    dok_nds_render_camera camera={0};
    ++frame;
    nds_map_update(&map,&world,&game,font,language,pixels,vram,frame*123u,1,7,frame,0,63,&camera,0);
    CHECK(!(g_nds_map_probe[2]&1u));CHECK(g_nds_map_probe[1]==2u);CHECK(g_nds_map_probe[5]==frame);
    CHECK(map.live[0]==frame);CHECK(!map.fault);CHECK(!memcmp(pixels,vram,sizeof(pixels)));
}
static void remember(void){
    memcpy(saved_planes,planes,sizeof(planes));memcpy(saved_pre,map.visited_snapshot,sizeof(saved_pre));
    memcpy(saved_post,post,sizeof(post));memcpy(saved_pixels,pixels,sizeof(pixels));
}
static void retained(void){
    CHECK(!memcmp(saved_planes,planes,sizeof(planes)));CHECK(!memcmp(saved_pre,map.visited_snapshot,sizeof(saved_pre)));
    CHECK(!memcmp(saved_post,post,sizeof(post)));CHECK(!memcmp(saved_pixels,pixels,sizeof(pixels)));
}
static void exact_comparisons(void){
    _Alignas(4) unsigned char a[160],b[160];
    for(unsigned ax=0;ax<4u;++ax)for(unsigned bx=0;bx<4u;++bx)for(unsigned n=0;n<=128u;++n){
        for(unsigned i=0;i<160u;++i)a[i]=(unsigned char)(i*37u+19u);
        memcpy(b+bx,a+ax,n);CHECK(!nds_map_test_bytes_differ(a+ax,b+bx,n));
        for(unsigned at=0;at<n;++at){b[bx+at]^=0x80u;CHECK(nds_map_test_bytes_differ(a+ax,b+bx,n));b[bx+at]^=0x80u;}
        b[bx+n]^=1u;CHECK(!nds_map_test_bytes_differ(a+ax,b+bx,n));
    }
}
static void all_class1_dimensions(void){
    static const unsigned char glyphs[6]={0,1,0x80,0x81,0xff,32};
    for(unsigned width=1;width<=64u;++width)for(unsigned length=1;length<=64u;++length){
        unsigned classes=(width+length)%6u,count=width*length,expected_writes=0;
        memset(&world,0,sizeof(world));memset(&game,0,sizeof(game));
        world.effective.settings[DOK_LEVEL_WIDTH]=(int16_t)width;
        world.effective.settings[DOK_LEVEL_LENGTH]=(int16_t)length;
        world.effective.object_count=(uint16_t)(classes+1u);
        world.effective.objects[0]=(dok_level_object){.glyph=32,.class_id=0,.marker=86};
        for(unsigned c=0;c<classes;++c)world.effective.objects[c+1u]=(dok_level_object){.glyph=glyphs[c],.class_id=1,.marker=2};
        memset(world.initial.maps[0],32,4096);memset(world.initial.maps[1],32,4096);memset(world.initial.maps[2],32,4096);
        memset(world.initial.maps[3],0x55,4096);
        for(unsigned n=0;n<count;++n){
            world.initial.maps[0][n]=glyphs[n%6u];world.initial.maps[3][n]=(uint8_t)(n%3u==0u);
            if(n%6u<classes&&!world.initial.maps[3][n])++expected_writes;
        }
        nds_map_load(&map,&world,&game,1);update(1);CHECK(map.class1_count==classes);CHECK(map.hero_visited==expected_writes);
        for(unsigned n=0;n<count;++n)CHECK(world.initial.maps[3][n]==(uint8_t)(n%3u==0u||n%6u<classes));
        for(unsigned n=count;n<4096u;++n)CHECK(world.initial.maps[3][n]==0x55u);
    }
}
static void split_presentation(void){
    dok_nds_render_camera camera={0};
    memset(&world,0,sizeof(world));memset(&game,0,sizeof(game));
    world.effective.settings[DOK_LEVEL_WIDTH]=64;world.effective.settings[DOK_LEVEL_LENGTH]=64;
    world.effective.object_count=3;
    world.effective.objects[0]=(dok_level_object){.glyph=32,.class_id=0,.marker=86};
    world.effective.objects[1]=(dok_level_object){.glyph=1,.class_id=1,.marker=2};
    world.effective.objects[2]=(dok_level_object){.glyph=4,.class_id=0,.marker=12};
    memset(world.initial.maps[0],32,4096);memset(world.initial.maps[1],32,4096);memset(world.initial.maps[2],32,4096);
    world.initial.maps[0][10]=1;world.initial.maps[2][10]=4;game.hero.x=480*10;
    nds_map_load(&map,&world,&game,1);memset(vram,77,sizeof(vram));memcpy(saved_pixels,vram,sizeof(vram));
    uint32_t before=dmas;++frame;
    nds_map_prepare(&map,&world,&game,font,1,pixels,123,1,7,frame,0,63,&camera,0);
    CHECK(g_nds_map_probe[2]&1u);CHECK(map.prepared);CHECK(dmas==before);
    CHECK(map.updates==0&&map.upload_bytes==0&&map.verified==0);
    CHECK(!memcmp(vram,saved_pixels,sizeof(vram)));CHECK(map.profile[4]==0&&map.profile[5]==0);
    CHECK(map.profile[10]==UINT32_MAX&&map.profile[11]==UINT32_MAX);
    CHECK(map.visited_snapshot[10]==0&&post[10]==1&&world.initial.maps[3][10]==1);
    CHECK(pixels[448]==86);uint32_t prepare_cpu=map.profile[7];
    clock_ticks+=BUS_CLOCK/5u;nds_map_transfer(&map,pixels,vram);
    CHECK(g_nds_map_probe[2]&1u);CHECK(map.prepared==2u&&dmas==before+1u);
    CHECK(map.updates==0&&map.upload_bytes==0&&map.verified==0);
    CHECK(!memcmp(vram,pixels,sizeof(vram)));
    nds_map_transfer(&map,pixels,vram);CHECK(dmas==before+1u);
    clock_ticks+=BUS_CLOCK/5u;nds_map_commit(&map,pixels,vram);
    CHECK(!(g_nds_map_probe[2]&1u)&&!map.prepared);CHECK(dmas==before+1u);
    CHECK(map.updates==1&&map.upload_bytes==49152);CHECK(!memcmp(vram,pixels,sizeof(vram)));
    CHECK(map.profile[4]>0&&map.profile[7]>=prepare_cpu&&map.profile[7]<BUS_CLOCK/10u);
    CHECK(map.profile[10]==192u&&map.profile[11]==192u);
    CHECK(g_nds_map_probe[33]==1&&map.live[41]==1&&g_nds_map_probe[5]==frame);
    nds_map_commit(&map,pixels,vram);CHECK(dmas==before+1u);
    /* Settle the required PRE->POST floor debt, then split a truly clean frame.
     * A simulated long VBlank wait cannot create DMA or inflate CPU stage7. */
    update(1);CHECK(pixels[448]==12);before=dmas;remember();++frame;
    nds_map_prepare(&map,&world,&game,font,1,pixels,456,1,7,frame,0,63,&camera,0);
    CHECK(g_nds_map_probe[2]&1u);CHECK(map.pending_reason==0&&map.updates==2);retained();
    CHECK(map.profile[10]==UINT32_MAX&&map.profile[11]==UINT32_MAX);
    CHECK(dmas==before);prepare_cpu=map.profile[7];clock_ticks+=BUS_CLOCK/2u;
    nds_map_commit(&map,pixels,vram);CHECK(!(g_nds_map_probe[2]&1u));
    CHECK(dmas==before&&map.profile[4]==0&&map.profile[5]==0);
    CHECK(map.profile[10]==UINT32_MAX&&map.profile[11]==UINT32_MAX);
    CHECK(map.profile[7]>=prepare_cpu&&map.profile[7]<BUS_CLOCK/10u);
    CHECK(g_nds_map_probe[5]==frame&&map.live[0]==frame);retained();
}
int main(void){
    world.effective.settings[DOK_LEVEL_WIDTH]=64;world.effective.settings[DOK_LEVEL_LENGTH]=64;
    world.effective.object_count=5;world.initial.epoch=42;
    world.effective.objects[0]=(dok_level_object){.glyph=32,.class_id=0,.marker=86};
    world.effective.objects[1]=(dok_level_object){.glyph=1,.class_id=1,.marker=2};
    world.effective.objects[2]=(dok_level_object){.glyph=2,.class_id=7,.marker=9};
    world.effective.objects[3]=(dok_level_object){.glyph=3,.class_id=9,.marker=10};
    world.effective.objects[4]=(dok_level_object){.glyph=4,.class_id=0,.marker=12};
    memset(world.initial.maps[0],32,4096);memset(world.initial.maps[1],32,4096);memset(world.initial.maps[2],32,4096);
    world.initial.maps[0][10]=1;world.initial.maps[2][10]=4;
    game.hero.x=480*10;game.hero.z=0;
    game.hero.heading=90;map.planes=planes;map.post_visited=post;
    nds_map_load(&map,&world,&game,1);update(1);
    CHECK(dmas==1&&map.updates==1&&map.upload_bytes==49152);
    CHECK(map.visited_snapshot[10]==0&&post[10]==1&&world.initial.maps[3][10]==1);
    CHECK(map.hero_visited==1);CHECK(pixels[256+192]==86);remember();
    /* PRE0->POST1 creates a real pending first-pass floor fill. It must be
     * shown once at the next dynamic deadline even in an otherwise idle world. */
    update(1);CHECK(dmas==1);retained();
    clock_ticks+=BUS_CLOCK/12u;update(1);
    CHECK(dmas==2&&map.visited_snapshot[10]==1&&post[10]==1);
    CHECK(pixels[256+192]==12);CHECK(memcmp(saved_pixels,pixels,sizeof(pixels))!=0);
    unsigned changed_pixels=0;for(unsigned n=0;n<40960u;++n)changed_pixels+=saved_pixels[n]!=pixels[n];
    CHECK(changed_pixels==13u);remember();
    uint32_t shown=map.last_draw_frame;
    for(unsigned i=0;i<20u;++i){clock_ticks+=BUS_CLOCK/12u;update(1);retained();}
    CHECK(dmas==2&&map.skips==21);CHECK(g_nds_map_probe[62]==shown);
    CHECK(map.profile[1]==0&&map.profile[3]==0&&map.profile[4]==0&&map.profile[5]==0);
    /* Change authoritative planes just after a draw: side effect must happen
     * immediately while every published scanout snapshot remains unchanged. */
    nds_map_invalidate(&map);update(1);remember();
    uint32_t before=dmas;world.initial.maps[0][11]=1;update(1);
    CHECK(dmas==before&&world.initial.maps[3][11]==1&&map.hero_visited==2);retained();
    CHECK(post[11]==0&&map.visited_snapshot[11]==0);CHECK(map.live[47]>0);
    clock_ticks+=BUS_CLOCK/12u;update(1);CHECK(dmas==before+1);
    CHECK(map.visited_snapshot[11]==1&&post[11]==1&&planes[11]==1);remember();
    /* Dynamic hero/door/corpse state is aggregated at 12Hz. */
    game.hero.heading=180;game.interactions.doors[2].ready=1;game.interactions.doors[2].marker=250;
    game.ai.count=1;game.ai.enemies[0].glyph=3;game.ai.enemies[0].corpse=1;
    before=dmas;update(1);CHECK(dmas==before);retained();
    CHECK(map.snapshot.heading==90);clock_ticks+=BUS_CLOCK/12u;update(1);
    CHECK(dmas==before+1&&map.snapshot.heading==180&&map.glyphs[2].marker==250&&map.glyphs[3].monster_state==4);
    /* Every touch sample reaches control. A changed viewport/button draws now,
     * even if the dynamic refresh deadline has not elapsed. */
    dok_world_clock clock={0};before=dmas;
    nds_map_input(&map,&clock,0,1,100,70,&game);update(1);CHECK(dmas==before);
    nds_map_input(&map,&clock,0,1,115,58,&game);update(1);CHECK(dmas==before+1&&!map.control.follow);
    CHECK(map.profile[4]>0);nds_map_input(&map,&clock,0,0,115,58,&game);update(1);
    before=dmas;nds_map_input(&map,&clock,0,1,110,177,&game);update(1);CHECK(dmas==before+1);
    CHECK(g_nds_map_probe[10]==4&&g_nds_map_probe[14]==3);CHECK(pixels[170*256+100]==2);
    before=dmas;nds_map_input(&map,&clock,0,1,110,177,&game);update(1);CHECK(dmas==before);
    nds_map_input(&map,&clock,0,0,110,177,&game);update(1);CHECK(dmas==before+1&&pixels[170*256+100]==86);
    before=dmas;update(2);CHECK(dmas==before+1&&map.language==2);
    before=dmas;map.input_pending=1;map.input_ticks=0;
    nds_map_invalidate(&map);CHECK(map.input_pending==0);update(2);CHECK(dmas==before+1);
    /* Start pause cancels a held gesture. World and audio ownership remain
     * external to this caching test; held touch cannot resume a prior drag. */
    nds_map_input(&map,&clock,0,1,100,70,&game);update(2);
    nds_map_input(&map,&clock,KEY_START,1,110,75,&game);update(2);CHECK(map.paused&&map.control.blocked);
    nds_map_input(&map,&clock,0,1,110,75,&game);update(2);
    nds_map_input(&map,&clock,KEY_START,1,110,75,&game);update(2);CHECK(!map.paused&&map.control.blocked);
    nds_map_input(&map,&clock,0,0,110,75,&game);update(2);CHECK(!map.control.blocked);
    /* 120Hz input/world sampling must aggregate actual dynamic draws to 12Hz.
     * Each skipped update still executes class1 writes in the live world. */
    nds_map_invalidate(&map);update(2);before=dmas;uint32_t visited_writes=map.hero_visited;
    for(unsigned i=0;i<120u;++i){
        clock_ticks+=BUS_CLOCK/120u;game.hero.heading=(int16_t)(i%360u);
        world.initial.maps[0][100u+i]=1;
        uint32_t previous=dmas;remember();update(2);
        CHECK(world.initial.maps[3][100u+i]==1);CHECK(map.hero_visited==visited_writes+i+1u);
        if(dmas==previous){retained();CHECK(map.profile[1]==0&&map.profile[4]==0);}
    }
    CHECK(dmas-before>=11u&&dmas-before<=13u);
    /* Flush the final pending dynamic state, then remain idle for two seconds. */
    clock_ticks+=BUS_CLOCK/12u;update(2);before=dmas;remember();
    for(unsigned i=0;i<24u;++i){clock_ticks+=BUS_CLOCK/12u;update(2);retained();}
    CHECK(dmas==before);
    /* Zoom buttons at every scale, including saturation, remain immediate.
     * Their held highlight changes even when the viewport is clamped. */
    for(unsigned i=0;i<10u;++i){
        int16_t x=i<5u?110:30;before=dmas;
        nds_map_input(&map,&clock,0,1,x,177,&game);update(2);CHECK(dmas==before+1u);
        CHECK(map.control.zoom<5u);CHECK(g_nds_map_probe[10]==dok_map_scales[map.control.zoom]);
        before=dmas;nds_map_input(&map,&clock,0,0,x,177,&game);update(2);CHECK(dmas==before+1u);
    }
    map.profile[16]=0x12345678u;update(2);CHECK(map.profile[16]==0x12345678u);
#if N21P_AUDIT
    CHECK(map.verified==map.updates);
#else
    CHECK(map.verified==0);
#endif
    CHECK(dmas==map.updates);
    uint32_t primary_draws=dmas,primary_skips=map.skips,primary_deferred=map.deferred;
    exact_comparisons();all_class1_dimensions();split_presentation();
    printf("N21P dirty map checks=%u draws=%u skips=%u deferred=%u PASS\n",checks,primary_draws,primary_skips,primary_deferred);
    return 0;
}
