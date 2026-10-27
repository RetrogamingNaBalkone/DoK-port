/* Bring-up for a hardware floor: software foreground becomes72 affine
 * 32x8 OBJs, preserving120 logical columns. BG0 carries the240-wide HUD.
 * This first mode keeps the software floor, to verify composition before
 * moving plane sampling to affine BG2. No OAM/HBlank-free timing shortcut. */
#ifndef DOK_RENDER_HW_DIRECT_PROTOTYPE
static uint8_t hw_tile_scratch[1024] __attribute__((aligned(4)));
static uint8_t hw_top_ui[1280] __attribute__((aligned(4)));
#endif
volatile uint32_t g_hw_obj_diag[6];
#ifdef DOK_RENDER_HW_WORLD_OBJ_PROTOTYPE
#include "dok/gba_world_obj.h"
static void hw_world_obj_vblank(void);
#endif
#ifdef DOK_RENDER_HW_ROWS96_PROTOTYPE
#define HW_SCENE_BYTES 12288
#define HW_OBJ_ROWS 12
#define HW_OBJ_STEP_Y 12
#define HW_OBJ_PD 170
#elif defined(DOK_RENDER_HW_ROWS72_PROTOTYPE)
#define HW_SCENE_BYTES 9216
#define HW_OBJ_ROWS 9
#define HW_OBJ_STEP_Y 16
#define HW_OBJ_PD 128
#elif defined(DOK_RENDER_HW_VERTICAL128_PROTOTYPE)
#define HW_SCENE_BYTES 16384
#define HW_OBJ_ROWS 16
#define HW_OBJ_STEP_Y 9
#define HW_OBJ_PD 227
#else
#define HW_SCENE_BYTES 18432
#define HW_OBJ_ROWS 18
#define HW_OBJ_STEP_Y 8
#define HW_OBJ_PD 256
#endif
#ifdef DOK_RENDER_HW_OBJ_ROWS_PROBE_PROTOTYPE
volatile uint32_t g_hw_obj_rows_probe[4]; /* request,ack,hold,source rows */
static void hw_obj_test_rows(void) {
    if(g_hw_obj_rows_probe[0]!=g_hw_obj_rows_probe[1]) {
        DISPCNT=0x80;
        for(unsigned y=0;y<HW_OBJ_ROWS*8;++y)for(unsigned x=0;x<128;x+=2) {
            unsigned offset=(y/8)*1024+(x/8)*64+(y&7)*8+(x&7);
            *(volatile uint16_t *)(uintptr_t)(0x06010000+offset)=(uint16_t)((y+1)*257);
        }
        PAL[0]=0;for(unsigned i=0;i<256;++i)PAL[256+i]=(uint16_t)i;
        DISPCNT=
#ifdef DOK_RENDER_HW_ROWS96_PROTOTYPE
            0x3041;
#else
            0x1041;
#endif
        g_hw_obj_rows_probe[3]=HW_OBJ_ROWS*8;
        g_hw_obj_rows_probe[1]=g_hw_obj_rows_probe[0];
    }
    while(g_hw_obj_rows_probe[2])__asm__ volatile("nop");
}
#endif
static unsigned hw_black_index;
static unsigned hw_video_ready,hw_palette_view;
#ifdef DOK_RENDER_HW_LOW_ADAPTIVE_PROTOTYPE
extern uint32_t g_hw_low_mode;
static unsigned hw_obj_last_ui;
#define HW_USE_UI_OBJECTS (g_hw_low_mode!=0)
#elif defined(DOK_RENDER_HW_UI_OBJ_PROTOTYPE)
#define HW_USE_UI_OBJECTS 1
#else
#define HW_USE_UI_OBJECTS 0
#endif
#ifdef DOK_RENDER_HW_BG_STREAM_PROTOTYPE
static uint32_t hw_ui_dirty[4],hw_weapon_dirty[3];
static __attribute__((target("arm"))) void hw_dirty_tiles(uint32_t *dirty,uintptr_t video,const uint8_t *stage,unsigned bytes) {
    for(unsigned i=0;i<(bytes/64+31)/32;++i)dirty[i]=0;
    for(unsigned tile=0;tile<bytes/64;++tile) {
        const uint32_t *src=(const uint32_t *)(const void *)(stage+tile*64);
        const volatile uint32_t *dst=(const volatile uint32_t *)(video+tile*64);
        unsigned changed=!hw_video_ready;
        if(!changed)for(unsigned word=0;word<16;++word)if(src[word]!=dst[word]){changed=1;break;}
        if(changed)dirty[tile>>5]|=1u<<(tile&31);
    }
}
#endif
#ifdef DOK_RENDER_HW_BG_PROTOTYPE
#ifdef DOK_RENDER_HW_UI_OBJ_PROTOTYPE
#ifdef DOK_RENDER_HW_LOW_ADAPTIVE_PROTOTYPE
#define HW_UI_VRAM (HW_USE_UI_OBJECTS?0x06014000u:0x0600e000u)
#else
#define HW_UI_VRAM 0x06014000
#endif
#else
#define HW_UI_VRAM 0x0600e000
#endif
#define HW_UI_TILE_BASE 896
#define HW_EMPTY_TILE 1023
#define HW_EMPTY_VRAM 0x0600ffc0
#else
#define HW_UI_VRAM 0x06002000
#define HW_UI_TILE_BASE 128
#define HW_EMPTY_TILE 127
#define HW_EMPTY_VRAM 0x06001fc0
#endif
#ifdef DOK_RENDER_HW_PACK_ARM_PROTOTYPE
extern void dok_gba_hw_compact(uint8_t *,unsigned);
extern void dok_gba_hw_tiles(uint8_t *,uint8_t *);
extern void dok_gba_hw_ui_tile(uint8_t *,const uint8_t *,unsigned,unsigned);
#endif

#ifdef DOK_RENDER_HW_DIRECT_PROTOTYPE
#include "hardware_direct.inc"
#else
#define HW_UI_STAGE 20000
#define HW_UI_BYTES 5120
#endif
static void hw_obj_black(void) {
    hw_black_index=0;
    for(unsigned i=1;i<256;++i) if(localized_ui[g_render_diag.language].colors[i]==localized_ui[g_render_diag.language].colors[0]) {hw_black_index=i;break;}
    if(!hw_black_index) panic(0xb0);
}
#ifndef DOK_RENDER_HW_DIRECT_PROTOTYPE
static void hw_copy_tile(uint8_t *dst,const uint8_t *src,unsigned stride) {
#ifdef DOK_RENDER_HW_PACK_ARM_PROTOTYPE
    dok_gba_hw_ui_tile(dst,src,stride,hw_black_index);
#else
    for(unsigned y=0;y<8;++y) for(unsigned x=0;x<8;++x) {
        unsigned c=src[y*stride+x];dst[y*8+x]=(uint8_t)(c?c:hw_black_index);
    }
#endif
}
#endif
static void hw_obj_prepare(void) {
#ifdef DOK_RENDER_HW_DIRECT_PROTOTYPE
    g_hw_obj_diag[0]=0x48574f42;g_hw_obj_diag[1]=timer_sample()-hw_direct_ui_stamp;
#ifdef DOK_RENDER_HW_BG_STREAM_PROTOTYPE
#ifdef DOK_RENDER_HW_UI_REUSE_PROTOTYPE
    extern unsigned g_hw_ui_changed;
    if(!g_hw_ui_changed && hw_video_ready
#ifdef DOK_RENDER_HW_LOW_ADAPTIVE_PROTOTYPE
       && hw_obj_last_ui==HW_USE_UI_OBJECTS
#endif
       )for(unsigned i=0;i<4;++i)hw_ui_dirty[i]=0;
    else
#endif
    hw_dirty_tiles(hw_ui_dirty,HW_UI_VRAM,g_render_frame.frame+HW_UI_STAGE,HW_UI_BYTES);
    if(g_hw_weapon[4])hw_dirty_tiles(hw_weapon_dirty,0x06006000,g_render_frame.frame+HW_WEAPON_STAGE,g_hw_weapon[4]);
#endif
#else
    uint32_t stamp=timer_sample();uint8_t *frame=g_render_frame.frame;
    hw_obj_black();
    for(unsigned y=0;y<16;++y) {
        for(unsigned x=0;x<48;x+=4) *(uint32_t *)(void *)(hw_top_ui+y*48+x)=*(const uint32_t *)(const void *)(frame+y*240+x);
        for(unsigned x=0;x<32;x+=4) *(uint32_t *)(void *)(hw_top_ui+768+y*32+x)=*(const uint32_t *)(const void *)(frame+y*240+208+x);
    }
#ifdef DOK_RENDER_HW_PACK_ARM_PROTOTYPE
    dok_gba_hw_compact(frame,hw_black_index);dok_gba_hw_tiles(frame,hw_tile_scratch);
#else
    for(unsigned y=0;y<144;++y) {
        for(unsigned x=0;x<120;++x) {unsigned c=frame[y*240+x*2];frame[y*128+x]=(uint8_t)(c?c:hw_black_index);}
        for(unsigned x=120;x<128;++x) frame[y*128+x]=0;
    }
    for(unsigned y=0;y<144;y+=8) {
        for(unsigned k=0;k<1024;++k) hw_tile_scratch[k]=frame[y*128+k];
        for(unsigned tx=0;tx<16;++tx) for(unsigned ry=0;ry<8;++ry)
            for(unsigned rx=0;rx<8;++rx) frame[y*128+tx*64+ry*8+rx]=hw_tile_scratch[ry*128+tx*8+rx];
    }
#endif
    uint8_t *ui=frame+20000;
    for(unsigned ty=0;ty<2;++ty) for(unsigned tx=0;tx<30;++tx)
        hw_copy_tile(ui+(ty*30+tx)*64,frame+(144+ty*8)*240+tx*8,240);
    for(unsigned ty=0;ty<2;++ty) for(unsigned tx=0;tx<6;++tx)
        hw_copy_tile(ui+(60+ty*6+tx)*64,hw_top_ui+ty*8*48+tx*8,48);
    for(unsigned ty=0;ty<2;++ty) for(unsigned tx=0;tx<4;++tx)
        hw_copy_tile(ui+(72+ty*4+tx)*64,hw_top_ui+768+ty*8*32+tx*8,32);
    g_hw_obj_diag[0]=0x48574f42;g_hw_obj_diag[1]=timer_sample()-stamp;
#endif
}
static void hw_obj_dma(uintptr_t dst,const void *source,unsigned bytes) {
#if defined(DOK_RENDER_HW_UI_OBJ_PROTOTYPE) || defined(DOK_RENDER_HW_WORLD_OBJ_PROTOTYPE)
    R16(0x040000de)=0;
#endif
    for(unsigned offset=0;offset<bytes;offset+=1024) {
        unsigned count=bytes-offset;if(count>1024) count=1024;
#if defined(DOK_RENDER_HW_UI_OBJ_PROTOTYPE) || defined(DOK_RENDER_HW_WORLD_OBJ_PROTOTYPE)
        /* VBlank also configures DMA3 for OAM streaming. Publish these three
         * immediate-transfer registers atomically; the DMA stall itself is
         * unchanged from the established bounded1024-byte upload. */
        unsigned ime=R16(0x04000208);R16(0x04000208)=0;R16(0x040000de)=0;
#endif
        *(volatile uint32_t *)0x040000d4=(uintptr_t)source+offset;
        *(volatile uint32_t *)0x040000d8=dst+offset;
        *(volatile uint32_t *)0x040000dc=0x84000000u|(count/4);
        __asm__ volatile("nop" ::: "memory");
#if defined(DOK_RENDER_HW_UI_OBJ_PROTOTYPE) || defined(DOK_RENDER_HW_WORLD_OBJ_PROTOTYPE)
        R16(0x04000208)=(uint16_t)ime;
#ifdef DOK_RENDER_SECTOR_BITMAP_PROTOTYPE
        /* Many120-byte row DMAs must leave a real IRQ service window. */
        __asm__ volatile("nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop" ::: "memory");
#endif
#endif
    }
}
#ifdef DOK_RENDER_HW_BG_STREAM_PROTOTYPE
static void hw_upload_dirty(uintptr_t video,const uint8_t *stage,unsigned bytes,const uint32_t *dirty) {
    for(unsigned tile=0;tile<bytes/64;) {
        if(!(dirty[tile>>5]&(1u<<(tile&31)))){++tile;continue;}
        unsigned start=tile++;
        while(tile<bytes/64 && (dirty[tile>>5]&(1u<<(tile&31))))++tile;
        hw_obj_dma(video+start*64,stage+start*64,(tile-start)*64);
    }
}
#endif
#ifdef DOK_RENDER_HW_BG_PROTOTYPE
#include "hardware_bg.inc"
#endif
#ifdef DOK_RENDER_HW_UI_OBJ_PROTOTYPE
#include "hardware_ui_obj.inc"
#endif
#ifdef DOK_RENDER_HW_WORLD_OBJ_PROTOTYPE
#include "hardware_world_obj.inc"
#endif
#ifdef DOK_RENDER_SECTOR_BITMAP_PROTOTYPE
#include "hardware_sector_bitmap.inc"
#endif
#ifdef DOK_RENDER_SECTOR_GPU_PROTOTYPE
#include "hardware_sector_gpu.inc"
#endif
#ifdef DOK_RENDER_HW_LOW_ADAPTIVE_PROTOTYPE
static void hw_obj_adaptive_layout(void) {
    volatile uint16_t *oam=(volatile uint16_t *)0x07000000;
    for(unsigned i=0;i<128;++i){oam[i*4]=0x200;oam[i*4+1]=0;oam[i*4+2]=0;oam[i*4+3]=0;}
    for(unsigned row=0;row<HW_OBJ_ROWS;++row)for(unsigned col=0;col<4;++col) {
        if(HW_USE_UI_OBJECTS && row)continue;
        unsigned id=row*4+col,slot=id+(HW_USE_UI_OBJECTS?64:0);
        oam[slot*4]=(uint16_t)(0x6300|((row*HW_OBJ_STEP_Y+HW_OBJ_STEP_Y/2-8)&255));
        oam[slot*4+1]=(uint16_t)(0x4000|col*64);oam[slot*4+2]=(uint16_t)(0x400|id*8);
    }
    oam[3]=128;oam[7]=0;oam[11]=0;oam[15]=HW_OBJ_PD;
    if(!HW_USE_UI_OBJECTS) {
        volatile uint16_t *map=(volatile uint16_t *)0x06007800;
        for(unsigned i=0;i<1024;++i)map[i]=HW_EMPTY_TILE;
        hw_old_keys=~0u;hw_old_weapon[0]=~0u;
    }
    for(unsigned i=0;i<32;++i)((volatile uint16_t *)HW_EMPTY_VRAM)[i]=0;
}
#endif
static void hw_obj_upload(void) {
#ifdef DOK_RENDER_SECTOR_GPU_PROTOTYPE
    hw_sector_gpu_upload();return;
#endif
#ifdef DOK_RENDER_SECTOR_BITMAP_PROTOTYPE
    hw_bitmap_upload();return;
#endif
    uint32_t stamp=timer_sample();
#ifdef DOK_RENDER_HW_BG_PROTOTYPE
    hw_bg_upload();
#endif
#ifndef DOK_RENDER_HW_LOW_ADAPTIVE_PROTOTYPE
    volatile uint16_t *oam=(volatile uint16_t *)0x07000000;
#endif
#if !defined(DOK_RENDER_HW_UI_OBJ_PROTOTYPE) || defined(DOK_RENDER_HW_LOW_ADAPTIVE_PROTOTYPE)
    volatile uint16_t *map=(volatile uint16_t *)0x06007800;
#endif
#ifdef DOK_RENDER_HW_LOW_ADAPTIVE_PROTOTYPE
    unsigned layout_changed=hw_obj_last_ui!=HW_USE_UI_OBJECTS;
    if(!hw_video_ready)hw_obj_adaptive_layout();
#else
    if(!hw_video_ready) {
    for(unsigned i=0;i<128;++i) {oam[i*4]=0x200;oam[i*4+1]=0;oam[i*4+2]=0;oam[i*4+3]=0;}
    for(unsigned row=0;row<HW_OBJ_ROWS;++row) for(unsigned col=0;col<4;++col) {
#ifdef DOK_RENDER_HW_UI_OBJ_PROTOTYPE
        if(row)continue;
#endif
        unsigned id=row*4+col;
        unsigned slot=id;
#ifdef DOK_RENDER_HW_UI_OBJ_PROTOTYPE
        slot+=64; /* Native HUD wins the scanline budget before hidden scene. */
#endif
        oam[slot*4]=(uint16_t)(0x6300|((row*HW_OBJ_STEP_Y+HW_OBJ_STEP_Y/2-8)&255));
        oam[slot*4+1]=(uint16_t)(0x4000|col*64);oam[slot*4+2]=(uint16_t)(0x400|id*8);
    }
    oam[3]=128;oam[7]=0;oam[11]=0;oam[15]=HW_OBJ_PD;
#ifndef DOK_RENDER_HW_UI_OBJ_PROTOTYPE
    for(unsigned i=0;i<1024;++i) map[i]=HW_EMPTY_TILE;
#endif
#ifndef DOK_RENDER_HW_DIRECT_PROTOTYPE
    for(unsigned y=0;y<2;++y) {
        for(unsigned x=0;x<30;++x) map[(18+y)*32+x]=(uint16_t)(128+y*30+x);
        for(unsigned x=0;x<6;++x) map[y*32+x]=(uint16_t)(188+y*6+x);
        for(unsigned x=0;x<4;++x) map[y*32+26+x]=(uint16_t)(200+y*4+x);
    }
#endif
    for(unsigned i=0;i<32;++i) ((volatile uint16_t *)HW_EMPTY_VRAM)[i]=0;
    }
#endif
    unsigned view=g_render_diag.level*2+g_render_diag.language+1;
    if(!hw_video_ready || hw_palette_view!=view) {
    for(unsigned i=0;i<256;++i) {uint16_t c=localized_ui[g_render_diag.language].colors[i];PAL[i]=c;PAL[256+i]=c;}
        hw_palette_view=view;
    }
    hw_obj_dma(0x06010000,g_render_frame.frame,HW_SCENE_BYTES);
#ifdef DOK_RENDER_HW_WORLD_OBJ_PROTOTYPE
    hw_world_obj_upload();
#endif
#ifdef DOK_RENDER_HW_EARLY_PRESENT_PROTOTYPE
    /* Scene/weapon/upper HUD are finished by line144. The bottom message
     * tiles must stay unchanged until their current scanlines are finished. */
    if(VCOUNT>=144 && VCOUNT<160)while(VCOUNT<160){}
#endif
#ifdef DOK_RENDER_HW_LOW_ADAPTIVE_PROTOTYPE
    if(layout_changed)hw_obj_adaptive_layout();
#endif
#ifdef DOK_RENDER_HW_BG_STREAM_PROTOTYPE
    hw_upload_dirty(HW_UI_VRAM,g_render_frame.frame+HW_UI_STAGE,HW_UI_BYTES,hw_ui_dirty);
#else
    hw_obj_dma(HW_UI_VRAM,g_render_frame.frame+HW_UI_STAGE,HW_UI_BYTES);
#endif
#ifdef DOK_RENDER_HW_DIRECT_PROTOTYPE
#ifdef DOK_RENDER_HW_UI_OBJ_PROTOTYPE
#ifdef DOK_RENDER_HW_LOW_ADAPTIVE_PROTOTYPE
    if(HW_USE_UI_OBJECTS) {hw_ui_objects();hw_ui_weapon_upload();}
    else {
        hw_direct_map(map);
        if(g_hw_weapon[4])hw_upload_dirty(0x06006000,g_render_frame.frame+HW_WEAPON_STAGE,g_hw_weapon[4],hw_weapon_dirty);
    }
#else
    hw_ui_objects();hw_ui_weapon_upload();
#endif
#else
    hw_direct_map(map);
#ifdef DOK_RENDER_HW_BG_STREAM_PROTOTYPE
    if(g_hw_weapon[4])hw_upload_dirty(0x06006000,g_render_frame.frame+HW_WEAPON_STAGE,g_hw_weapon[4],hw_weapon_dirty);
#else
    if(g_hw_weapon[4]) hw_obj_dma(0x06006000,g_render_frame.frame+HW_WEAPON_STAGE,g_hw_weapon[4]);
#endif
#endif
#endif
    R16(0x04000008)=0x0f80;R16(0x04000010)=0;R16(0x04000012)=0;
#ifdef DOK_RENDER_HW_ROWS96_PROTOTYPE
    /* PD170 keeps adjacent12-line bands covered. Its last fixed-point sample
     * extends one row below the scene; WIN0 clips OBJ to the native144 rows. */
    R16(0x04000040)=240;R16(0x04000044)=144;
    R16(0x04000048)=0x3f;R16(0x0400004a)=0x2f;
#endif
    hw_video_ready=1;
#ifdef DOK_RENDER_HW_LOW_ADAPTIVE_PROTOTYPE
    hw_obj_last_ui=HW_USE_UI_OBJECTS;
#endif
#if defined(DOK_RENDER_HW_UI_OBJ_PROTOTYPE) || defined(DOK_RENDER_HW_WORLD_OBJ_PROTOTYPE)
    gba_hw_bg_vblank(); /* Immediate texture DMA3 uploads have now finished. */
#endif
#ifdef DOK_RENDER_HW_BG_PROTOTYPE
    g_hw_bg_diag[5]=VCOUNT;
    if(!hw_bg_cold_frame && VCOUNT<160)++g_hw_bg_diag[4];
#endif
    g_hw_obj_diag[2]=timer_sample()-stamp;++g_hw_obj_diag[3];
    g_hw_obj_diag[4]=VCOUNT;g_hw_obj_diag[5]=HW_OBJ_ROWS*4;
}
#ifdef DOK_RENDER_HW_PRESENT_PROTOTYPE
volatile uint32_t g_hw_present_diag[4]; /* estimated cycles,start VCOUNT,late accepted,late rejected */
static unsigned hw_upload_budget(void) {
    if(!hw_video_ready || hw_bg_stream_cold)return 0xffffffffu;
    unsigned bytes=0;
    for(unsigned tile=0;tile<HW_UI_BYTES/64;++tile)if(hw_ui_dirty[tile>>5]&(1u<<(tile&31)))bytes+=64;
    for(unsigned tile=0;tile<g_hw_weapon[4]/64;++tile)if(hw_weapon_dirty[tile>>5]&(1u<<(tile&31)))bytes+=64;
    unsigned blocks=0;
    for(unsigned mat=0;mat<dok_hw_bg_counts[hw_bg_view];++mat) {
        unsigned phase=0;
        if(dok_hw_bg_animated[hw_bg_view][mat]) {
            const dok_runtime_object *o=&g_render_world.objects[dok_hw_bg_objects[hw_bg_view][mat]];
            phase=(o->known&DOK_RUNTIME_CROP_PHASE)?(uint16_t)o->animation_a%18u:o->frame%18u;
        }
        if(phase!=hw_bg_phases[mat])blocks+=3;
    }
    /* Full foreground DMA plus map maintenance, IRQ allowance and guard;
     * dirty UI/weapon bytes, prepared map cells and animated texture blocks.
     * This is deliberately above the measured cost, never a CPU-time target. */
    unsigned fixed=55000;
#ifdef DOK_RENDER_HW_MAP_CACHE_PROTOTYPE
    if(!hw_direct_map_dirty())fixed=53000;
#endif
#ifdef DOK_RENDER_HW_UI_OBJ_PROTOTYPE
    if(HW_USE_UI_OBJECTS)fixed+=12000;
#ifdef DOK_RENDER_HW_LOW_ADAPTIVE_PROTOTYPE
    if(hw_obj_last_ui!=HW_USE_UI_OBJECTS)fixed+=3000;
#endif
#endif
#ifdef DOK_RENDER_HW_VERTICAL128_PROTOTYPE
    fixed-=(18432-HW_SCENE_BYTES)*2;
#endif
#ifdef DOK_RENDER_HW_WORLD_OBJ_PROTOTYPE
    fixed+=4096;
    if(g_world_obj_state)for(unsigned slot=0;slot<DOK_WORLD_OBJ_SLOTS;++slot)
        if(g_world_obj_state->used[slot] && g_world_obj_state->uploaded[slot]!=g_world_obj_state->slots[slot])fixed+=4096;
#endif
    return fixed+bytes*2+hw_bg_command_count*320+blocks*2400;
}
#endif
