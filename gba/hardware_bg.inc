/* Mode1 affine floor/base ceiling. One512x512 toroidal map per plane/band;
 * HBlank selects a map and inverse-plane transform for the next scanline. */
extern const uint8_t dok_hw_bg_bank[],dok_hw_bg_counts[6],dok_hw_bg_animated[6][8],dok_hw_bg_maps[6][2][256];
extern const uint16_t dok_hw_bg_objects[6][8];
extern const uint32_t dok_hw_bg_offsets[6][8][18][3];
extern const uint8_t dok_static_cells[6][3][4096];
extern int g_hw_bg_envelope[4];
#ifdef DOK_RENDER_HW_BG_LOWERED_PROTOTYPE
extern const uint8_t dok_hw_low_maps[6][256];
#endif
#ifdef DOK_RENDER_HW_OAM_RAM_PROTOTYPE
#include "dok/gba_hw_scratch.h"
#define HW_OAM_ROWS ((uint32_t *)(void *)(((uintptr_t)g_render_frame.covered+sizeof(g_render_frame.covered)-DOK_HW_OAM_FRAME_BYTES)&~(uintptr_t)3))
#endif
typedef struct {int16_t pa,pb,pc,pd;int32_t x,y;} hw_bg_row;
_Static_assert(sizeof(hw_bg_row)==16,"HBlank matrix DMA layout");
#ifdef DOK_RENDER_HW_LOW_ADAPTIVE_PROTOTYPE
typedef struct {hw_bg_row rows[161];uint16_t cnt[161];} hw_bg_single_frame;
static volatile unsigned hw_bg_dual[2],hw_bg_mode4_dual;
#endif
#ifdef DOK_RENDER_HW_BG_LOWERED_PROTOTYPE
#include "dok/gba_hw_scratch.h"
typedef struct {hw_bg_row base,low;} hw_bg_dual_row;
typedef struct {hw_bg_dual_row rows[161];uint32_t cnt[161];} hw_bg_frame;
_Static_assert(sizeof(hw_bg_frame)*2==DOK_HW_LOW_FRAME_BYTES,"dual affine scratch partition");
static hw_bg_frame hw_bg_mode4_hold;
static volatile unsigned hw_bg_mode4_held;
#define hw_bg_frames ((hw_bg_frame *)(void *)(((uintptr_t)g_render_frame.covered+sizeof(g_render_frame.covered)-DOK_HW_LOW_FRAME_BYTES-DOK_HW_OAM_RESERVED_BYTES)&~(uintptr_t)3))
#define HW_BG_BASE(f,y) ((f)->rows[y].base)
#else
typedef struct {hw_bg_row rows[161];uint16_t cnt[161];} hw_bg_frame;
static hw_bg_frame hw_bg_frames[2];
#define HW_BG_BASE(f,y) ((f)->rows[y])
#endif
static volatile unsigned hw_bg_front,hw_bg_enabled;
static unsigned hw_bg_next,hw_bg_view,hw_bg_loaded_view,hw_bg_loaded;
static int hw_bg_x,hw_bg_z,hw_bg_loaded_x,hw_bg_loaded_z;
static unsigned hw_bg_blank_x,hw_bg_blank_z;
static uint8_t hw_bg_phases[8];
static unsigned hw_bg_cold_frame;
volatile uint32_t g_hw_bg_diag[8]; /* magic,cold loads,map cells,texture KiB,overflows,exit VCOUNT,rows,wait cycles */
volatile uint32_t g_hw_bg_prepare_cycles;
#ifdef DOK_RENDER_HW_BG_STREAM_PROTOTYPE
typedef struct {uint32_t offset,first[6];} hw_bg_command;
static unsigned hw_bg_command_count,hw_bg_stream_cold;
extern void dok_gba_hw_map_cells(const hw_bg_command *,unsigned);
static void hw_bg_plan_cells(void) {
    hw_bg_command *cmd=(hw_bg_command *)(void *)(g_render_frame.frame+30720);
    hw_bg_command_count=0;
    hw_bg_stream_cold=!hw_bg_loaded || hw_bg_loaded_view!=hw_bg_view || !hw_video_ready ||
        hw_bg_x-hw_bg_loaded_x>=16 || hw_bg_loaded_x-hw_bg_x>=16 || hw_bg_z-hw_bg_loaded_z>=16 || hw_bg_loaded_z-hw_bg_z>=16;
    if(!hw_bg_stream_cold && hw_bg_x==hw_bg_loaded_x && hw_bg_z==hw_bg_loaded_z)return;
    for(int z=hw_bg_z;z<hw_bg_z+16;++z)for(int x=hw_bg_x;x<hw_bg_x+16;++x) {
        if(!hw_bg_stream_cold && x>=hw_bg_loaded_x && x<hw_bg_loaded_x+16 && z>=hw_bg_loaded_z && z<hw_bg_loaded_z+16)continue;
        hw_bg_command *c=&cmd[hw_bg_command_count++];
        unsigned cell=(unsigned)z*64+(unsigned)x,glyph=g_render_world.maps[8192+cell];
        c->offset=((unsigned)z&15)*256+((unsigned)x&15)*4;
        for(unsigned plane=0;plane<2;++plane) {
            unsigned mat=dok_static_cells[hw_bg_view][0][cell]==2?255:dok_hw_bg_maps[hw_bg_view][plane][glyph];
            for(unsigned band=0;band<3;++band)c->first[plane*3+band]=mat==255?0:
                0x03020100u+(1+(mat+(band==1?dok_hw_bg_counts[hw_bg_view]:0))*16)*0x01010101u;
        }
#ifdef DOK_RENDER_HW_BG_LOWERED_PROTOTYPE
        unsigned mat=dok_static_cells[hw_bg_view][0][cell]==2?255:dok_hw_low_maps[hw_bg_view][glyph];
        for(unsigned band=0;band<2;++band)c->first[band?5:2]=mat==255?0:
            0x03020100u+(1+(mat+(band?dok_hw_bg_counts[hw_bg_view]:0))*16)*0x01010101u;
#endif
    }
}
#endif

void gba_hw_bg_vblank(void) {
    if(!hw_bg_enabled)return;
    R16(0x040000ba)=0;R16(0x040000d2)=0;
    hw_bg_frame *f=
#ifdef DOK_RENDER_HW_BG_LOWERED_PROTOTYPE
        hw_bg_mode4_held?&hw_bg_mode4_hold:
#endif
        &hw_bg_frames[hw_bg_front];
    volatile uint32_t *regs=(volatile uint32_t *)
#ifdef DOK_RENDER_SECTOR_GPU_PROTOTYPE
        0x04000030;
#else
        0x04000020;
#endif
    const uint32_t *row=(const uint32_t *)(const void *)&f->rows[0];
#ifdef DOK_RENDER_HW_LOW_ADAPTIVE_PROTOTYPE
    unsigned dual=hw_bg_mode4_held?hw_bg_mode4_dual:hw_bg_dual[hw_bg_front];
    if(!dual) {
        const hw_bg_single_frame *single=(const hw_bg_single_frame *)(const void *)f;
        for(unsigned i=0;i<4;++i)regs[i]=row[i];
        R16(0x0400000c)=single->cnt[0];
        *(volatile uint32_t *)0x040000b0=(uintptr_t)&single->rows[1];
        *(volatile uint32_t *)0x040000b4=0x04000020;
        *(volatile uint32_t *)0x040000b8=0xa6600004u;
        *(volatile uint32_t *)0x040000c8=(uintptr_t)&single->cnt[1];
        *(volatile uint32_t *)0x040000cc=0x0400000c;
        *(volatile uint32_t *)0x040000d0=0xa2400001u;
    } else {
#endif
#ifdef DOK_RENDER_HW_BG_LOWERED_PROTOTYPE
    for(unsigned i=0;i<8;++i)regs[i]=row[i];
    *(volatile uint32_t *)0x0400000c=f->cnt[0];
#else
    for(unsigned i=0;i<4;++i)regs[i]=row[i];
    R16(
#ifdef DOK_RENDER_SECTOR_GPU_PROTOTYPE
        0x0400000e
#else
        0x0400000c
#endif
    )=f->cnt[0];
#endif
    *(volatile uint32_t *)0x040000b0=(uintptr_t)&f->rows[1];
    *(volatile uint32_t *)0x040000b4=
#ifdef DOK_RENDER_SECTOR_GPU_PROTOTYPE
        0x04000030;
#else
        0x04000020;
#endif
#ifdef DOK_RENDER_HW_BG_LOWERED_PROTOTYPE
    *(volatile uint32_t *)0x040000b8=0xa6600008u;
#else
    *(volatile uint32_t *)0x040000b8=0xa6600004u;
#endif
    *(volatile uint32_t *)0x040000c8=(uintptr_t)&f->cnt[1];
    *(volatile uint32_t *)0x040000cc=
#ifdef DOK_RENDER_SECTOR_GPU_PROTOTYPE
        0x0400000e;
#else
        0x0400000c;
#endif
#ifdef DOK_RENDER_HW_BG_LOWERED_PROTOTYPE
    *(volatile uint32_t *)0x040000d0=0xa6400001u;
#else
    *(volatile uint32_t *)0x040000d0=0xa2400001u;
#endif
#ifdef DOK_RENDER_HW_LOW_ADAPTIVE_PROTOTYPE
    }
#endif
#if defined(DOK_RENDER_HW_UI_OBJ_PROTOTYPE) && !defined(DOK_RENDER_SECTOR_GPU_PROTOTYPE)
#ifdef DOK_RENDER_HW_LOW_ADAPTIVE_PROTOTYPE
    R16(0x040000de)=0;
    if(dual) {
#endif
    /* Multiplex four scene OBJs instead of evaluating two overlapping affine
     * bands on a scanline. DMA3 can read this constant OAM table from ROM. */
    extern const uint32_t dok_hw_obj_row_oam[161][8];
    const uint32_t *oam_rows=
#ifdef DOK_RENDER_HW_OAM_RAM_PROTOTYPE
        hw_bg_mode4_held?&dok_hw_obj_row_oam[0][0]:HW_OAM_ROWS;
#else
        &dok_hw_obj_row_oam[0][0];
#endif
    R16(0x040000de)=0;
    for(unsigned i=0;i<8;++i)((volatile uint32_t *)0x07000200)[i]=oam_rows[i];
    *(volatile uint32_t *)0x040000d4=(uintptr_t)(oam_rows+8);
    *(volatile uint32_t *)0x040000d8=0x07000200;
    *(volatile uint32_t *)0x040000dc=0xa6600008u;
#ifdef DOK_RENDER_HW_LOW_ADAPTIVE_PROTOTYPE
    }
#endif
#endif
#ifdef DOK_RENDER_HW_WORLD_OBJ_PROTOTYPE
    hw_world_obj_vblank();
#endif
}
#ifdef DOK_RENDER_HW_BG_LOWERED_PROTOTYPE
static void hw_bg_hold_before_mode4(void) {
    if(!hw_bg_enabled || hw_bg_mode4_held)return;
    const uint32_t *src=(const uint32_t *)(const void *)&hw_bg_frames[hw_bg_front];
    uint32_t *dst=(uint32_t *)(void *)&hw_bg_mode4_hold;
    for(unsigned i=0;i<sizeof(hw_bg_mode4_hold)/4;++i)dst[i]=src[i];
    /* The old GPU frame remains visible while Mode4 builds its scene. Swap
     * DMA sources in VBlank before the legacy renderer reuses covered[]. */
    while(VCOUNT<160){}
#ifdef DOK_RENDER_HW_LOW_ADAPTIVE_PROTOTYPE
    hw_bg_mode4_dual=hw_bg_dual[hw_bg_front];
#endif
    hw_bg_mode4_held=1;gba_hw_bg_vblank();
}
#endif
static void hw_bg_disable(void) {
    hw_bg_enabled=0;R16(0x040000ba)=0;R16(0x040000d2)=0;
#if defined(DOK_RENDER_HW_UI_OBJ_PROTOTYPE) || defined(DOK_RENDER_HW_WORLD_OBJ_PROTOTYPE)
    R16(0x040000de)=0;
#endif
    R16(0x04000020)=256;R16(0x04000022)=0;R16(0x04000024)=0;R16(0x04000026)=256;
    *(volatile uint32_t *)0x04000028=0;*(volatile uint32_t *)0x0400002c=0;
}
#ifdef DOK_RENDER_HW_ROW_LUT_PROTOTYPE
typedef struct {int16_t pa,pc;int32_t x,y;} hw_bg_lut_entry;
_Static_assert(sizeof(hw_bg_lut_entry)==12,"plane LUT layout");
extern unsigned g_hw_bg_heading;
extern const hw_bg_lut_entry dok_hw_bg_row_matrix[360][144];
extern const uint16_t dok_hw_bg_row_cnt[144];
static void hw_bg_fill_lut(hw_bg_frame *f) {
    const hw_bg_lut_entry *lut=dok_hw_bg_row_matrix[g_hw_bg_heading];
    int32_t cx=camera.x*256/15,cz=camera.z*256/15;
    for(unsigned y=0;y<161;++y) {
        hw_bg_row *r=&HW_BG_BASE(f,y);r->pb=r->pd=0;
        if(y<144 && dok_hw_bg_row_cnt[y]) {
            r->pa=lut[y].pa;r->pc=lut[y].pc;r->x=lut[y].x+cx;r->y=lut[y].y+cz;f->cnt[y]=dok_hw_bg_row_cnt[y];
        }else {r->pa=r->pc=0;r->x=(int32_t)hw_bg_blank_x*256;r->y=(int32_t)hw_bg_blank_z*256;f->cnt[y]=0xb082;}
    }
}
#ifdef DOK_RENDER_HW_LOW_ADAPTIVE_PROTOTYPE
static void hw_bg_fill_single(hw_bg_single_frame *f) {
    const hw_bg_lut_entry *lut=dok_hw_bg_row_matrix[g_hw_bg_heading];
    int32_t cx=camera.x*256/15,cz=camera.z*256/15;
    for(unsigned y=0;y<161;++y) {
        hw_bg_row *r=&f->rows[y];r->pb=r->pd=0;
        if(y<144 && dok_hw_bg_row_cnt[y]) {
            r->pa=lut[y].pa;r->pc=lut[y].pc;r->x=lut[y].x+cx;r->y=lut[y].y+cz;f->cnt[y]=dok_hw_bg_row_cnt[y];
        }else {r->pa=r->pc=0;r->x=(int32_t)hw_bg_blank_x*256;r->y=(int32_t)hw_bg_blank_z*256;f->cnt[y]=0xb082;}
    }
}
#endif
#endif
#ifdef DOK_RENDER_HW_BG_LOWERED_PROTOTYPE
extern const hw_bg_lut_entry dok_hw_low_row_matrix[360][144];
extern const uint16_t dok_hw_low_row_cnt[144];
static void hw_bg_fill_low(hw_bg_frame *f) {
    const hw_bg_lut_entry *lut=dok_hw_low_row_matrix[g_hw_bg_heading];
    int32_t cx=camera.x*256/15,cz=camera.z*256/15;
    for(unsigned y=0;y<161;++y) {
        hw_bg_row *r=&f->rows[y].low;
        unsigned cnt=0xb481;
        *r=(hw_bg_row){0,0,0,0,(int32_t)hw_bg_blank_x*256,(int32_t)hw_bg_blank_z*256};
        if(y<144 && camera.y==-300 && dok_hw_low_row_cnt[y]) {
            r->pa=lut[y].pa;r->pc=lut[y].pc;r->x=lut[y].x+cx;r->y=lut[y].y+cz;cnt=dok_hw_low_row_cnt[y];
        }else if(y<144 && camera.y!=-300) {
            int dy=(int)y*10/9-camera.center_y;
            if(dy && ((-561-camera.y<0)==(dy<0))) {
                int distance=(-561-camera.y)*200/dy,depth=distance-200;
                if(depth>0 && depth<=3600) {
                    int32_t px=camera.cosine*distance/18000,pz=camera.sine*distance/18000;
                    r->pa=(int16_t)(px/16);r->pc=(int16_t)(pz/16);
                    r->x=cx-camera.sine*depth/1920-px*477/64;r->y=cz+camera.cosine*depth/1920-pz*477/64;
                    unsigned light=dok_renderer_light_row(depth,1920),band=light<4?0:light<11?1:2;
                    unsigned screen=band==1?26:20;
                    cnt=0xa081|(screen<<8)|(band==2?4:0);
                }
            }
        }
        f->cnt[y]|=(uint32_t)cnt<<16;
    }
}
#endif
static __attribute__((target("arm"))) void hw_bg_prepare(void) {
    uint32_t stamp=timer_sample();
#ifdef DOK_RENDER_HW_OAM_RAM_PROTOTYPE
    if(!hw_video_ready) {
        extern const uint32_t dok_hw_obj_row_oam[161][8];
        const uint32_t *source=&dok_hw_obj_row_oam[0][0];
        for(unsigned i=0;i<DOK_HW_OAM_FRAME_BYTES/4;++i)HW_OAM_ROWS[i]=source[i];
    }
#endif
    hw_bg_view=g_render_diag.level*2+g_render_diag.language;
    hw_bg_x=g_hw_bg_envelope[0]-(16-(g_hw_bg_envelope[1]-g_hw_bg_envelope[0]+1))/2;
    hw_bg_z=g_hw_bg_envelope[2]-(16-(g_hw_bg_envelope[3]-g_hw_bg_envelope[2]+1))/2;
    if(hw_bg_x<0)hw_bg_x=0;if(hw_bg_x>48)hw_bg_x=48;
    if(hw_bg_z<0)hw_bg_z=0;if(hw_bg_z>48)hw_bg_z=48;
#ifdef DOK_RENDER_HW_BG_STREAM_PROTOTYPE
    /* A turning camera changes the envelope centre without changing the
     * visible window. Retain resident cells until an edge actually escapes. */
    if(hw_bg_loaded && hw_bg_loaded_view==hw_bg_view && hw_video_ready) {
        hw_bg_x=hw_bg_loaded_x;hw_bg_z=hw_bg_loaded_z;
        if(g_hw_bg_envelope[0]<hw_bg_x)hw_bg_x=g_hw_bg_envelope[0];
        if(g_hw_bg_envelope[1]>=hw_bg_x+16)hw_bg_x=g_hw_bg_envelope[1]-15;
        if(g_hw_bg_envelope[2]<hw_bg_z)hw_bg_z=g_hw_bg_envelope[2];
        if(g_hw_bg_envelope[3]>=hw_bg_z+16)hw_bg_z=g_hw_bg_envelope[3]-15;
        if(hw_bg_x<0)hw_bg_x=0;if(hw_bg_x>48)hw_bg_x=48;
        if(hw_bg_z<0)hw_bg_z=0;if(hw_bg_z>48)hw_bg_z=48;
    }
#endif
#ifdef DOK_RENDER_HW_BG_STREAM_PROTOTYPE
    hw_bg_plan_cells();
#endif
    unsigned found=0;
    for(int z=hw_bg_z;z<hw_bg_z+16 && !found;++z)for(int x=hw_bg_x;x<hw_bg_x+16;++x)
        if(dok_static_cells[hw_bg_view][0][z*64+x]==2){hw_bg_blank_x=((unsigned)x&15)*32+4;hw_bg_blank_z=((unsigned)z&15)*32+4;found=1;break;}
    if(!found)panic(0xb2); /* Offline check proves every current16-cell window has walls. */
    hw_bg_next=hw_bg_front^1;hw_bg_frame *f=&hw_bg_frames[hw_bg_next];
#ifdef DOK_RENDER_HW_LOW_ADAPTIVE_PROTOTYPE
    hw_bg_dual[hw_bg_next]=g_hw_low_mode;
    if(!g_hw_low_mode && camera.y==-300) {
        hw_bg_fill_single((hw_bg_single_frame *)(void *)f);
        g_hw_bg_diag[0]=0x48424731;g_hw_bg_diag[6]=144;
        g_hw_bg_prepare_cycles=timer_sample()-stamp;return;
    }
#endif
#ifdef DOK_RENDER_HW_ROW_LUT_PROTOTYPE
    if(camera.y==-300) {
        hw_bg_fill_lut(f);
#ifdef DOK_RENDER_HW_BG_LOWERED_PROTOTYPE
        hw_bg_fill_low(f);
#endif
        g_hw_bg_diag[0]=0x48424731;g_hw_bg_diag[6]=144;
        g_hw_bg_prepare_cycles=timer_sample()-stamp;return;
    }
#endif
    for(unsigned y=0;y<161;++y) {
        hw_bg_row *row=&HW_BG_BASE(f,y);*row=(hw_bg_row){0,0,0,0,(int32_t)hw_bg_blank_x*256,(int32_t)hw_bg_blank_z*256};
        f->cnt[y]=0xb082;
        if(y>=144)continue;
        int dy=(int)y*10/9-camera.center_y,plane=dy<0;
        int height=plane?-951:0;
        if(!dy || ((height-camera.y<0)!=(dy<0)))continue;
#ifdef DOK_RENDER_HW_RAYS32_PROTOTYPE
        extern const uint16_t dok_hw_bg_distances[144];
        int distance=camera.y==-300?dok_hw_bg_distances[y]:(height-camera.y)*200/dy;
#else
        int distance=(height-camera.y)*200/dy;
#endif
        int depth=distance-200;
        if(depth<=0 || depth>3600)continue;
#ifdef DOK_RENDER_HW_RAYS32_PROTOTYPE
        /* All products stay signed32: distance<=3800 and sin/cos<=32768.
         * Sixteen fractional PA steps retain the starting reference within
         *0.03 texture pixel; output PA/PC are the same8.8 quantization. */
        int32_t px=(camera.cosine*distance)/18000,pz=(camera.sine*distance)/18000;
        row->pa=(int16_t)(px/16);row->pc=(int16_t)(pz/16);
        row->x=camera.x*256/15-(camera.sine*depth)/1920-px*477/64;
        row->y=camera.z*256/15+(camera.cosine*depth)/1920-pz*477/64;
#else
        int32_t lateral=-distance*52101,step=distance*65536/75;
        int32_t wx=(camera.x*65536)/480+((int32_t)(((int64_t)lateral*camera.cosine)>>15))/480-(camera.sine*depth*2)/480;
        int32_t wz=(camera.z*65536)/480+((int32_t)(((int64_t)lateral*camera.sine)>>15))/480+(camera.cosine*depth*2)/480;
        int32_t dx=((int32_t)(((int64_t)step*camera.cosine)>>15))/480;
        int32_t dz=((int32_t)(((int64_t)step*camera.sine)>>15))/480;
        row->pa=(int16_t)(dx/16);row->pc=(int16_t)(dz/16);row->x=wx/8;row->y=wz/8;
#endif
        unsigned light=dok_renderer_light_row(depth,1920),band=light<4?0:light<11?1:2;
#ifdef DOK_RENDER_HW_BG_LOWERED_PROTOTYPE
        unsigned screen=16+plane*6+(band==1?2:0);
#else
        unsigned screen=16+plane*6+band*2;
#endif
        f->cnt[y]=(uint16_t)(0xa082|(screen<<8)|(band==2?4:0));
    }
#ifdef DOK_RENDER_HW_BG_LOWERED_PROTOTYPE
#ifdef DOK_RENDER_HW_LOW_ADAPTIVE_PROTOTYPE
    if(g_hw_low_mode)
#endif
    hw_bg_fill_low(f);
#endif
#ifdef DOK_RENDER_HW_LOW_ADAPTIVE_PROTOTYPE
    if(!g_hw_low_mode) {
        hw_bg_single_frame *single=(hw_bg_single_frame *)(void *)f;
        for(unsigned y=0;y<161;++y)single->rows[y]=f->rows[y].base;
        for(unsigned y=0;y<161;++y)single->cnt[y]=(uint16_t)f->cnt[y];
    }
#endif
    g_hw_bg_diag[0]=0x48424731;g_hw_bg_diag[6]=144;
    g_hw_bg_prepare_cycles=timer_sample()-stamp;
}
#ifndef DOK_RENDER_HW_BG_STREAM_PROTOTYPE
static void hw_bg_cell(unsigned x,unsigned z) {
    unsigned cell=z*64+x,glyph=g_render_world.maps[8192+cell];
    unsigned blocked=dok_static_cells[hw_bg_view][0][cell]==2;
    for(unsigned plane=0;plane<2;++plane) {
        unsigned mat=blocked?255:dok_hw_bg_maps[hw_bg_view][plane][glyph];
        for(unsigned band=0;band<3;++band) {
            unsigned screen=16+plane*6+band*2;
            volatile uint32_t *dst=(volatile uint32_t *)(uintptr_t)(0x06000000+screen*2048+((z&15)*4)*64+(x&15)*4);
            uint32_t first=mat==255?0:0x03020100u+(1+(mat+(band==1?dok_hw_bg_counts[hw_bg_view]:0))*16)*0x01010101u;
            for(unsigned row=0;row<4;++row) {dst[row*16]=first;if(mat!=255)first+=0x04040404u;}
        }
    }
    ++g_hw_bg_diag[2];
}
#endif
static void hw_bg_upload(void) {
    g_hw_bg_diag[2]=g_hw_bg_diag[3]=g_hw_bg_diag[7]=0;
#ifdef DOK_RENDER_HW_BG_STREAM_PROTOTYPE
    unsigned cold=hw_bg_stream_cold;
#else
    unsigned cold=!hw_bg_loaded || hw_bg_loaded_view!=hw_bg_view || !hw_video_ready ||
        hw_bg_x-hw_bg_loaded_x>1 || hw_bg_loaded_x-hw_bg_x>1 || hw_bg_z-hw_bg_loaded_z>1 || hw_bg_loaded_z-hw_bg_z>1;
#endif
    hw_bg_cold_frame=cold;
    if(cold) {
        DISPCNT=0x80;++g_hw_bg_diag[1];
        for(unsigned i=0;i<32;++i) {((volatile uint16_t *)0x06000000)[i]=0;((volatile uint16_t *)0x06004000)[i]=0;}
#ifndef DOK_RENDER_HW_BG_STREAM_PROTOTYPE
        for(int z=hw_bg_z;z<hw_bg_z+16;++z)for(int x=hw_bg_x;x<hw_bg_x+16;++x)hw_bg_cell((unsigned)x,(unsigned)z);
#endif
        for(unsigned i=0;i<8;++i)hw_bg_phases[i]=255;
#ifdef DOK_RENDER_HW_BG_STREAM_PROTOTYPE
    }
    if(hw_bg_command_count)dok_gba_hw_map_cells((const hw_bg_command *)(const void *)(g_render_frame.frame+30720),hw_bg_command_count);
    g_hw_bg_diag[2]=hw_bg_command_count;
#else
    } else if(hw_bg_x!=hw_bg_loaded_x || hw_bg_z!=hw_bg_loaded_z) {
        for(int z=hw_bg_z;z<hw_bg_z+16;++z)for(int x=hw_bg_x;x<hw_bg_x+16;++x)
            if(x<hw_bg_loaded_x || x>=hw_bg_loaded_x+16 || z<hw_bg_loaded_z || z>=hw_bg_loaded_z+16)hw_bg_cell((unsigned)x,(unsigned)z);
    }
#endif
    for(unsigned mat=0;mat<dok_hw_bg_counts[hw_bg_view];++mat) {
        unsigned phase=0;
        if(dok_hw_bg_animated[hw_bg_view][mat]) {
            const dok_runtime_object *o=&g_render_world.objects[dok_hw_bg_objects[hw_bg_view][mat]];
            phase=(o->known&DOK_RUNTIME_CROP_PHASE)?(uint16_t)o->animation_a%18u:o->frame%18u;
        }
        if(phase==hw_bg_phases[mat])continue;
        for(unsigned band=0;band<3;++band) {
            uintptr_t dst=0x06000000u+(band==2?16384:0)+64+(mat+(band==1?dok_hw_bg_counts[hw_bg_view]:0))*1024;
            const uint8_t *src=dok_hw_bg_bank+dok_hw_bg_offsets[hw_bg_view][mat][phase][band];
#ifdef DOK_RENDER_SECTOR_GPU_PROTOTYPE
            if(band==2) {
                /* BG2 map occupies far atlas tile indices96..99. Shift its
                 * tail by four tiles, matching the map-command permutation. */
                unsigned first=dst<0x06005800u?(unsigned)(0x06005800u-dst):0;if(first>1024)first=1024;
                if(first)hw_obj_dma(dst,src,first);
                if(first<1024)hw_obj_dma(dst+first+256,src+first,1024-first);
            }else hw_obj_dma(dst,src,1024);
#else
            hw_obj_dma(dst,src,1024);
#endif
            ++g_hw_bg_diag[3];
        }
        hw_bg_phases[mat]=(uint8_t)phase;
    }
    hw_bg_loaded=1;hw_bg_loaded_view=hw_bg_view;hw_bg_loaded_x=hw_bg_x;hw_bg_loaded_z=hw_bg_z;
    __asm__ volatile("" ::: "memory");hw_bg_front=hw_bg_next;
#ifdef DOK_RENDER_HW_BG_LOWERED_PROTOTYPE
    hw_bg_mode4_held=0;
#endif
    hw_bg_enabled=1;gba_hw_bg_vblank();
    g_hw_bg_diag[5]=VCOUNT;
#ifdef DOK_RENDER_HW_EARLY_PRESENT_PROTOTYPE
    if(!cold && VCOUNT<144)++g_hw_bg_diag[4];
    if(cold || VCOUNT<144) {
#else
    if(!cold && VCOUNT<160)++g_hw_bg_diag[4];
    if(cold || VCOUNT<160) {
#endif
        uint32_t stamp=timer_sample();while(VCOUNT>=160){}while(VCOUNT<160){}
        g_hw_bg_diag[7]=timer_sample()-stamp;
    }
}
