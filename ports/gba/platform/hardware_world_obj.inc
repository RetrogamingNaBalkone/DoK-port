/* Direct-source OBJ companion to the normal Mode1 floor and BG0 HUD. */
static void hw_world_obj_vblank(void) {
    extern const uint32_t dok_hw_obj_row_oam[161][8];
    R16(0x040000de)=0;
    for(unsigned i=0;i<8;++i)((volatile uint32_t *)0x07000020)[i]=dok_hw_obj_row_oam[0][i];
    *(volatile uint32_t *)0x040000d4=(uintptr_t)&dok_hw_obj_row_oam[1][0];
    *(volatile uint32_t *)0x040000d8=0x07000020;
    *(volatile uint32_t *)0x040000dc=0xa6600008u;
}
static void hw_world_obj_upload(void) {
    dok_gba_world_obj_state *o=g_world_obj_state;
    if(!o || o->magic!=DOK_WORLD_OBJ_MAGIC)panic(0xb6);
    uint32_t stamp=timer_sample();unsigned bytes=0;
    hw_obj_dma(0x07000000,o->oam,1024);
    for(unsigned slot=0;slot<DOK_WORLD_OBJ_SLOTS;++slot)if(o->used[slot] && (!hw_video_ready || o->uploaded[slot]!=o->slots[slot])) {
        hw_obj_dma(0x06014000+slot*512,o->slots[slot],512);
        o->uploaded[slot]=o->slots[slot];bytes+=512;
    }
    g_world_obj_work[9]=bytes;g_world_obj_work[10]=timer_sample()-stamp;
    R16(0x04000040)=240;R16(0x04000044)=144;
    R16(0x04000048)=0x3f;R16(0x0400004a)=0x2f;
}
