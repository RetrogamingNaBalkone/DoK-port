#define main inherited_main
#define dmaCopy inherited_dmaCopy
#include "../../tests/test_map_runtime.c"
#undef dmaCopy
#undef main
static size_t last_dma;
void dmaCopy(const void *a,void *b,size_t n){last_dma=n;inherited_dmaCopy(a,b,n);}
static void redraw(uint32_t language)
{
    ++game.hero.heading;map.force=2;clock_ticks+=BUS_CLOCK/12u;update(language);
}
int main(void)
{
    if(inherited_main())return 1;
    memset(&world,0,sizeof(world));memset(&game,0,sizeof(game));memset(&map,0,sizeof(map));
    world.effective.settings[DOK_LEVEL_WIDTH]=64;world.effective.settings[DOK_LEVEL_LENGTH]=64;
    world.effective.object_count=1;world.effective.objects[0]=(dok_level_object){.glyph=32,.marker=86};
    memset(world.initial.maps[0],32,4096);memset(world.initial.maps[1],32,4096);memset(world.initial.maps[2],32,4096);
    for(unsigned n=0;n<sizeof(font);++n)font[n]=(unsigned char)(n*79u+3u);
    map.planes=planes;map.post_visited=post;nds_map_load(&map,&world,&game,1);update(1);
    CHECK(last_dma==49152u);redraw(1);CHECK(last_dma==40960u);
    CHECK(!memcmp(pixels,vram,sizeof(pixels)));
#if N23_MAP_SECTOR_DIRTY
    memset(&world,0,sizeof(world));memset(&game,0,sizeof(game));memset(&map,0,sizeof(map));
    world.effective.settings[DOK_LEVEL_WIDTH]=64;world.effective.settings[DOK_LEVEL_LENGTH]=64;
    world.effective.object_count=2;world.effective.objects[0]=(dok_level_object){.glyph=32,.marker=86};
    world.effective.objects[1]=(dok_level_object){.glyph=42,.class_id=6,.marker=2};
    memset(world.initial.maps[0],32,4096);memset(world.initial.maps[1],32,4096);memset(world.initial.maps[2],32,4096);
    world.initial.maps[0][0]=42;map.planes=planes;map.post_visited=post;
    nds_map_load(&map,&world,&game,1);update(1);
    unsigned char expected[DOK_MAP_VIEW_BYTES];
    for(unsigned heading=1;heading<=360u;++heading){
        game.hero.heading=(int16_t)heading;clock_ticks+=BUS_CLOCK/12u;update(1);
        dok_map_snapshot wanted=map.snapshot;wanted.heading=(uint16_t)heading;
        CHECK(dok_map_raster(expected,&wanted,dok_map_scales[map.control.zoom],-map.left,-map.top));
        CHECK(!memcmp(expected,pixels,sizeof(expected)));CHECK(map.live[8]==heading);
    }
    CHECK(map.updates==9u);
#endif
    map.control.follow=0;
    for(unsigned capture=0;capture<=5u;++capture){
        map.control.capture=capture;redraw(1);
        CHECK(last_dma==((capture==0u||capture==1u)?40960u:49152u));
        redraw(1);CHECK(last_dma==40960u);
    }
    font['-'*8u]^=0x80u;redraw(1);CHECK(last_dma==49152u);redraw(1);CHECK(last_dma==40960u);
    font['C'*8u]^=0x80u;redraw(1);CHECK(last_dma==40960u);
    redraw(2);CHECK(last_dma==49152u);redraw(2);CHECK(last_dma==40960u);
    font['C'*8u]^=0x40u;redraw(2);CHECK(last_dma==49152u);
    memset(vram,77,sizeof(vram));nds_map_invalidate(&map);update(2);CHECK(last_dma==49152u);
    CHECK(!memcmp(pixels,vram,sizeof(pixels)));
    printf("{\"passed\":true,\"checks\":%u,\"scope\":\"Existing production map lifetime suite plus retained button pixels/font/capture/language/invalidation\"}\n",checks);
    return 0;
}
