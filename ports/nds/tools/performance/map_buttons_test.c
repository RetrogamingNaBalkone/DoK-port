#define main inherited_main
#define dmaCopy inherited_dmaCopy
#define DC_FlushRange inherited_flush
#include "../../tests/test_map_runtime.c"
#undef DC_FlushRange
#undef dmaCopy
#undef main
static size_t last_dma;
static uint32_t snapshot_flush;
void dmaCopy(const void *a,void *b,size_t n){last_dma=n;inherited_dmaCopy(a,b,n);}
void DC_FlushRange(void *p,size_t n)
{
    if(p==planes||p==post||p==map.visited_snapshot||p==map.glyphs||p==map.palette)
        snapshot_flush+=(uint32_t)n;
    inherited_flush(p,n);
}
static void redraw(uint32_t language)
{
    ++game.hero.heading;map.force=2;clock_ticks+=BUS_CLOCK/12u;update(language);
}
static void tracked_hero(void)
{
    memset(&world,0,sizeof(world));memset(&game,0,sizeof(game));memset(&map,0,sizeof(map));
    world.effective.settings[DOK_LEVEL_WIDTH]=64;
    world.effective.settings[DOK_LEVEL_LENGTH]=64;
    world.effective.settings[DOK_LEVEL_CELL_SIDE]=480;
    world.effective.object_count=2;
    world.effective.objects[0]=(dok_level_object){.glyph=' ',.marker=86};
    world.effective.objects[1]=(dok_level_object){.glyph=1,.class_id=1,.marker=2};
    world.initial.definition=&world.effective;
    world.initial.hero.object_id=2;
    game.items.world=&world.initial;
    memset(world.initial.maps[DOK_WORLD_MMAP],' ',4096);
    memset(world.initial.maps[DOK_WORLD_TMAP],' ',4096);
    memset(world.initial.maps[DOK_WORLD_FCMAP],' ',4096);
    unsigned char *mmap=world.initial.maps[DOK_WORLD_MMAP];
    unsigned char *visited=world.initial.maps[DOK_WORLD_VISITED];
    mmap[0]=1;mmap[4095]=1;
    map.planes=planes;map.post_visited=post;
    nds_map_load(&map,&world,&game,1);update(1);
    CHECK(map.track_hero_cell&&map.hero_visited==2);
    CHECK(map.visited_snapshot[0]==0&&post[0]==1&&visited[4095]==1);
    mmap[0]=' ';mmap[65]=1;
    game.hero.x=480;game.hero.z=480;update(1);
    CHECK(map.hero_visited==3&&visited[65]==1&&map.updates==1);
    update(1);CHECK(map.hero_visited==3);
    /* A diagnostic pose can disagree with MMap. Keep full-scan semantics. */
    mmap[65]=' ';mmap[66]=1;update(1);
    CHECK(map.hero_visited==4&&visited[66]==1);
    /* A new level starts with a fresh PRE/POST pair. */
    memset(visited,0,4096);
    nds_map_load(&map,&world,&game,2);update(1);
    CHECK(map.hero_visited==2&&map.visited_snapshot[66]==0&&post[66]==1);
}
#if !N21P_AUDIT
static void deferred_redraw(void)
{
    memset(&world,0,sizeof(world));memset(&game,0,sizeof(game));memset(&map,0,sizeof(map));
    world.effective.settings[DOK_LEVEL_WIDTH]=64;
    world.effective.settings[DOK_LEVEL_LENGTH]=64;
    world.effective.object_count=1;
    world.effective.objects[0]=(dok_level_object){.glyph=' ',.marker=86};
    for(unsigned i=0;i<3u;++i)memset(world.initial.maps[i],' ',4096);
    map.planes=planes;map.post_visited=post;
    nds_map_load(&map,&world,&game,1);update(1);
    map.control.follow=0;
    dok_nds_render_camera camera={0};
    for(unsigned superseded=0;superseded<2u;++superseded){
        uint32_t before=dmas,old_vis=map.updates;
        uint32_t staged_cell=64u+superseded,new_cell=128u+superseded;
        world.initial.maps[DOK_WORLD_VISITED][staged_cell]=1;
        game.hero.heading=90+90*superseded;
        clock_ticks+=BUS_CLOCK/12u;++frame;
        nds_map_prepare(&map,&world,&game,font,1,pixels,123,1,7,frame,0,63,&camera,0);
        uint32_t staged=frame;
        CHECK(map.pending_upload_bytes&&nds_map_defer_transfer(&map));
        CHECK(dmas==before&&map.updates==old_vis&&(g_nds_map_probe[2]&1u));
        CHECK(!nds_map_defer_transfer(&map));
        memcpy(saved_pixels,pixels,sizeof(pixels));
        game.hero.heading+=45;
        world.initial.maps[DOK_WORLD_VISITED][new_cell]=1;
        if(superseded){
            map.force=2;map.input_pending=1;map.input_ticks=clock_ticks;
        }
        ++frame;
        nds_map_prepare(&map,&world,&game,font,1,pixels,124,1,7,frame,0,63,&camera,0);
        CHECK(!nds_map_defer_transfer(&map)&&map.updates==old_vis);
        CHECK(map.pending_probe[62]==(superseded?frame:staged));
        if(!superseded)CHECK(!memcmp(saved_pixels,pixels,sizeof(pixels)));
        nds_map_transfer(&map,pixels,vram);nds_map_commit(&map,pixels,vram);
        CHECK(dmas==before+1u&&map.updates==old_vis+1u&&!map.transfer_deferred);
        CHECK(!(g_nds_map_probe[2]&1u)&&g_nds_map_probe[62]==(superseded?frame:staged));
        CHECK(map.visited_snapshot[staged_cell]==1u&&post[staged_cell]==1u);
        CHECK(map.visited_snapshot[new_cell]==superseded&&post[new_cell]==superseded);
        CHECK(map.live[0]==frame&&!memcmp(pixels,vram,sizeof(pixels)));
        CHECK(!map.input_pending);
    }
}
#endif
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
    unsigned before_turn_bytes=map.upload_bytes;
    for(unsigned heading=1;heading<=360u;++heading){
        game.hero.heading=(int16_t)heading;clock_ticks+=BUS_CLOCK/12u;update(1);
        dok_map_snapshot wanted=map.snapshot;wanted.heading=(uint16_t)heading;
        CHECK(dok_map_raster(expected,&wanted,dok_map_scales[map.control.zoom],-map.left,-map.top));
        CHECK(!memcmp(expected,pixels,sizeof(expected)));CHECK(map.live[8]==heading);
    }
    CHECK(map.updates==9u);
    CHECK(map.upload_bytes-before_turn_bytes<=8u*3u*256u);
    snapshot_flush=0;
    game.hero.heading=90;clock_ticks+=BUS_CLOCK/12u;update(1);
    CHECK(map.updates==10u&&snapshot_flush==0);
    world.initial.maps[DOK_WORLD_VISITED][20]=1;
    clock_ticks+=BUS_CLOCK/12u;update(1);
    CHECK(snapshot_flush==20480u&&map.pre_visited_count==1u&&g_nds_map_probe[34]==1u);
    snapshot_flush=0;test_sub_palette[1]=0x12u;
    nds_map_invalidate(&map);
    redraw(1); /* Touch redraw can supersede force, but not palette dirtiness. */
    CHECK(snapshot_flush==512u&&map.palette[1]==test_sub_palette[1]);
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
    tracked_hero();
#if !N21P_AUDIT
    deferred_redraw();
#endif
    printf("{\"passed\":true,\"checks\":%u,\"scope\":\"Map lifetime, retained buttons, tracked hero cell and mismatch fallback\"}\n",checks);
    return 0;
}
