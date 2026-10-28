#include "dok/gba_sector_gpu.h"
static void hw_sector_gpu_prepare(void) {
    hw_bg_frame *f=&hw_bg_frames[hw_bg_next];
    for(unsigned y=0;y<161;++y)f->cnt[y]=(uint16_t)(((f->cnt[y]-0x400u)&~3u)|3u);
}
static void hw_sector_gpu_upload(void) {
    uint32_t stamp=timer_sample();hw_bg_upload();
    volatile uint16_t *oam=(volatile uint16_t *)0x07000000;
    if(!hw_video_ready) {
        for(unsigned id=0;id<128;++id){oam[id*4]=0x200;oam[id*4+1]=oam[id*4+2]=oam[id*4+3]=0;}
        volatile uint16_t *map=(volatile uint16_t *)0x06005800;
        for(unsigned i=0;i<128;++i)map[i]=(uint16_t)(i*2|((i*2+1)<<8));
    }
    unsigned view=g_render_diag.level*2+g_render_diag.language+1;
    if(!hw_video_ready || hw_palette_view!=view){
        for(unsigned i=0;i<256;++i){uint16_t c=localized_ui[g_render_diag.language].colors[i];PAL[i]=c;PAL[256+i]=c;}
        hw_palette_view=view;
    }
    hw_obj_dma(0x0600c000,g_render_frame.frame,16384);
    hw_upload_dirty(0x06014000,g_render_frame.frame+HW_UI_STAGE,HW_UI_BYTES,hw_ui_dirty);
    hw_ui_objects();hw_ui_weapon_upload();
    for(unsigned id=64;id<128;++id){oam[id*4]=0x200;oam[id*4+1]=oam[id*4+2]=0;}
    dok_sector_gpu_state *g=g_sector_gpu;
    if(g && g->valid && !g->failed){
        if(g->count>64 || g->bytes>16384 || g->matrices>32)panic(0xb6);
        hw_obj_dma(0x06010000,g->texture,g->bytes);
        for(unsigned id=0;id<g->count;++id)for(unsigned k=0;k<3;++k)oam[(id+64)*4+k]=g->oam[id][k];
#ifdef DOK_RENDER_SECTOR_GPU_SHEAR_PROTOTYPE
        for(unsigned m=0;m<g->matrices;++m){oam[m*16+3]=(uint16_t)g->matrix[m][0];oam[m*16+7]=0;oam[m*16+11]=(uint16_t)g->pc[m];oam[m*16+15]=(uint16_t)g->matrix[m][1];}
#else
        for(unsigned m=0;m<g->matrices;++m){oam[m*16+3]=(uint16_t)g->matrix[m][0];oam[m*16+7]=oam[m*16+11]=0;oam[m*16+15]=(uint16_t)g->matrix[m][1];}
#endif
    }
    R16(0x0400000c)=0x0b0e; /* CPU foreground BG2: character bank3, map11, priority2. */
    R16(0x04000020)=128;R16(0x04000022)=0;R16(0x04000024)=0;R16(0x04000026)=228;
    *(volatile uint32_t *)0x04000028=0;*(volatile uint32_t *)0x0400002c=0;
    hw_video_ready=1;gba_hw_bg_vblank();
    g_hw_obj_diag[0]=0x48574f42;g_hw_obj_diag[2]=timer_sample()-stamp;++g_hw_obj_diag[3];g_hw_obj_diag[4]=VCOUNT;
    g_hw_obj_diag[5]=g && !g->failed?g->count:0;
}
