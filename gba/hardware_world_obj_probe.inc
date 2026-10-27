/* Diagnostic-only source-art OBJ upload. The host writes ordinary EWRAM;
 * actual ARM stores update PPU/OAM/palette state and invalidate core caches.
 * No timing result from this hold is a renderer benchmark. */
volatile uint32_t g_world_obj_probe[4]; /* request,ack,hold,ready */
static void hw_world_obj_probe(void) {
    if(!g_world_obj_probe[2])return;
    unsigned ime=R16(0x04000208);R16(0x04000208)=0;
    unsigned multiplex=0,synced=0;
    g_world_obj_probe[3]=1;
    while(g_world_obj_probe[2]) {
        if(g_world_obj_probe[0]!=g_world_obj_probe[1]) {
            DISPCNT=0x80;
            for(unsigned i=0;i<4;++i)R16(0x040000ba+i*12)=0;
            const uint16_t *stage=(const uint16_t *)(const void *)g_render_frame.frame;
            volatile uint16_t *tiles=(volatile uint16_t *)0x06010000;
            volatile uint16_t *oam=(volatile uint16_t *)0x07000000;
            for(unsigned i=0;i<16384;++i)tiles[i]=stage[i];
            for(unsigned i=0;i<512;++i)oam[i]=stage[16384+i];
            for(unsigned i=0;i<256;++i)PAL[256+i]=stage[16896+i];
            multiplex=stage[17152]&2;synced=0;
            if((stage[17152]&1) && !multiplex) {
                /* The established120x128 CPU layer occupies slots0..63 and
                 * texture bytes0..16383. World OBJ use the remaining halves.
                 * Equal OBJ priority and lower OAM slot keep CPU pixels nearer. */
                for(unsigned row=0;row<16;++row)for(unsigned col=0;col<4;++col) {
                    unsigned id=row*4+col;
                    unsigned used=0;
                    for(unsigned k=0;k<128;++k)used|=stage[id*128+k];
                    if(!used) {oam[id*4]=0x200;continue;}
                    oam[id*4]=(uint16_t)(0x6300|((row*9+4-8)&255));
                    oam[id*4+1]=(uint16_t)(0x4000|col*64);
                    oam[id*4+2]=(uint16_t)(0x400|id*8);
                }
                oam[3]=128;oam[7]=0;oam[11]=0;oam[15]=227;
            }
            if(multiplex) {oam[3]=128;oam[7]=0;oam[11]=0;oam[15]=227;}
            PAL[0]=0x4210;
            R16(0x04000040)=240;R16(0x04000044)=144;
            R16(0x04000048)=0x3f;R16(0x0400004a)=0x2f;
            R16(0x04000050)=0;DISPCNT=(uint16_t)(multiplex?0x3060:0x3040);
            g_world_obj_probe[1]=g_world_obj_probe[0];
        }
        if(multiplex) {
            if(VCOUNT>=160 && !synced) {
                /* Reuse the established ROM row table, but CPU slots4..7
                 * leave matrix0 intact. Matrix1 is overwritten by the DMA
                 * padding, so direct objects deliberately start at matrix2. */
                extern const uint32_t dok_hw_obj_row_oam[161][8];
                R16(0x040000de)=0;
                for(unsigned i=0;i<8;++i)((volatile uint32_t *)0x07000020)[i]=dok_hw_obj_row_oam[0][i];
                *(volatile uint32_t *)0x040000d4=(uintptr_t)&dok_hw_obj_row_oam[1][0];
                *(volatile uint32_t *)0x040000d8=0x07000020;
                *(volatile uint32_t *)0x040000dc=0xa6600008u;
                synced=1;
            } else if(VCOUNT<160)synced=0;
        }
        __asm__ volatile("nop");
    }
    g_world_obj_probe[3]=0;R16(0x04000208)=(uint16_t)ime;
}
