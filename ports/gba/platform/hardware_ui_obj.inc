/* Native-resolution HUD/weapon on normal OBJs, leaving both affine BGs
 * available in Mode2. Scene retains its64 affine OBJs and matrix0. */
volatile uint32_t g_hw_ui_obj_count;
static void hw_ui_object(unsigned *id,unsigned x,unsigned y,unsigned tiles,unsigned tile) {
    if(*id>=64)panic(0xb4);
    volatile uint16_t *obj=(volatile uint16_t *)(uintptr_t)(0x07000000+*id*8);
    obj[0]=(uint16_t)(0x2000|(tiles>1?0x4000:0)|(y&255));
    obj[1]=(uint16_t)((tiles==4?0x4000:0)|(x&511));
    obj[2]=(uint16_t)tile;++*id;
}
static unsigned hw_ui_tile_nonzero(const uint8_t *source) {
    const uint32_t *words=(const uint32_t *)(const void *)source;
    for(unsigned n=0;n<16;++n)if(words[n])return 1;
    return 0;
}
static void hw_ui_object_row(unsigned *id,unsigned x,unsigned y,unsigned count,unsigned tile,const uint8_t *source) {
    while(count) {
        if(!hw_ui_tile_nonzero(source)){x+=8;tile+=2;source+=64;--count;continue;}
        unsigned run=1;
        while(run<count && hw_ui_tile_nonzero(source+run*64))++run;
        count-=run;source+=run*64;
        while(run) {
            unsigned n=run>=4?4:run>=2?2:1;
            hw_ui_object(id,x,y,n,tile);x+=n*8;tile+=n*2;run-=n;
        }
    }
}
static void hw_ui_objects(void) {
    volatile uint16_t *oam=(volatile uint16_t *)0x07000000;
    for(unsigned id=0;id<64;++id)oam[id*4]=0x200;
    unsigned id=0;
    const uint8_t *ui=g_render_frame.frame+HW_UI_STAGE;
    for(unsigned y=0;y<2;++y) {
        hw_ui_object_row(&id,0,144+y*8,30,512+y*60,ui+y*30*64);
        hw_ui_object_row(&id,0,y*8,9,512+(60+y*9)*2,ui+(60+y*9)*64);
        hw_ui_object_row(&id,176,y*8,8,512+(78+y*8)*2,ui+(78+y*8)*64);
    }
    unsigned keys=0;for(unsigned i=0;i<3;++i)if(g_render_world.player.keys&(1u<<i))++keys;
    for(unsigned y=0;y<2;++y)if(keys)
        hw_ui_object_row(&id,240-keys*16,128+y*8,keys*2,512+(94+y*6+6-keys*2)*2,ui+(94+y*6+6-keys*2)*64);
    if(g_hw_weapon[4]) {
        unsigned pitch=(g_hw_weapon[2]+3)&~3u;
        for(unsigned y=0;y<g_hw_weapon[3];++y)
            hw_ui_object_row(&id,g_hw_weapon[0],g_hw_weapon[1]+y*8,g_hw_weapon[2],768+y*pitch*2,g_render_frame.frame+HW_WEAPON_STAGE+y*g_hw_weapon[2]*64);
    }
    g_hw_ui_obj_count=64+id;
}
static void hw_ui_weapon_upload(void) {
    if(!g_hw_weapon[4])return;
    unsigned width=g_hw_weapon[2],pitch=(width+3)&~3u,rows=g_hw_weapon[3];
    if(pitch*rows*64>8192)panic(0xb5);
    for(unsigned y=0;y<rows;++y) {
        uintptr_t dst=0x06016000+y*pitch*64;
        hw_obj_dma(dst,g_render_frame.frame+HW_WEAPON_STAGE+y*width*64,width*64);
        volatile uint32_t *tail=(volatile uint32_t *)(dst+width*64);
        for(unsigned n=0;n<(pitch-width)*16;++n)tail[n]=0;
    }
}
