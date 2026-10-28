/* True Mode4 affine framebuffer, no scene OBJ packing or HBlank multiplexer.
 * CPU scene120x128/pitch128 is copied into the inactive240-pitch page. */
static unsigned hw_bitmap_page;
static void hw_bitmap_upload(void) {
    uint32_t stamp=timer_sample();
    hw_bg_disable();
    R16(0x040000de)=0;
    if(!hw_video_ready) {
        DISPCNT=0x80;
        for(unsigned i=0;i<128;++i)((volatile uint16_t *)0x07000000)[i*4]=0x200;
        for(unsigned i=0;i<0x14000/4;++i)((volatile uint32_t *)0x06000000)[i]=0;
        hw_bitmap_page=0;
    }
    unsigned view=g_render_diag.level*2+g_render_diag.language+1;
    if(!hw_video_ready || hw_palette_view!=view) {
        for(unsigned i=0;i<256;++i){unsigned c=localized_ui[g_render_diag.language].colors[i];PAL[i]=(uint16_t)c;PAL[256+i]=(uint16_t)c;}
        hw_palette_view=view;
    }
    hw_bitmap_page^=1;
    for(unsigned y=0;y<128;++y)hw_obj_dma(0x06000000+hw_bitmap_page*0xa000+y*240,g_render_frame.frame+y*128,120);
    hw_obj_dma(0x06014000,g_render_frame.frame+HW_UI_STAGE,HW_UI_BYTES);
    hw_ui_weapon_upload();hw_ui_objects();
    R16(0x0400000c)=1; /* BG2 behind native UI OBJ. */
    R16(0x04000020)=128;R16(0x04000022)=0;R16(0x04000024)=0;R16(0x04000026)=228;
    *(volatile uint32_t *)0x04000028=0;*(volatile uint32_t *)0x0400002c=0;
    hw_video_ready=1;
    g_hw_obj_diag[0]=0x48574f42;g_hw_obj_diag[2]=timer_sample()-stamp;++g_hw_obj_diag[3];
    g_hw_obj_diag[4]=VCOUNT;g_hw_obj_diag[5]=0;
}
