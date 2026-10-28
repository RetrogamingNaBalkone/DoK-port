/* Private hardware backend helpers. Scene pixels already use OBJ tile order;
 * BG0 retains the full-resolution weapon and every HUD widget. */
extern uint32_t g_hw_tile_active,g_hw_tile_black;
unsigned g_hw_weapon[5]; /* x,y,columns,rows,bytes; pixels at25600 */
#if !defined(DOK_RENDER_HW_UI_OBJ_PROTOTYPE) || defined(DOK_RENDER_HW_LOW_ADAPTIVE_PROTOTYPE)
static unsigned hw_old_weapon[4];
#ifdef DOK_RENDER_HW_MAP_CACHE_PROTOTYPE
static unsigned hw_old_keys;
static unsigned hw_direct_map_dirty(void) {
    if(!hw_video_ready || hw_old_keys!=g_render_world.player.keys)return 1;
    for(unsigned i=0;i<4;++i)if(hw_old_weapon[i]!=(g_hw_weapon[4]?g_hw_weapon[i]:0))return 1;
    return 0;
}
#endif
#endif
static uint32_t hw_direct_ui_stamp;
extern void dok_gba_tiled_clear(uint8_t *,unsigned);
#define HW_UI_STAGE 18432
#define HW_UI_BYTES 6784
#define HW_WEAPON_STAGE 25600

int gba_hw_weapon_begin(int left,int top,int width,int height) {
    int right=left+width,bottom=top+height;
    if(left<0) left=0;if(top<0) top=0;if(right>240) right=240;if(bottom>144) bottom=144;
    if(left>=right || top>=bottom) return 0;
    left&=~7;top&=~7;right=(right+7)&~7;bottom=(bottom+7)&~7;
    unsigned cols=(unsigned)(right-left)/8,rows=(unsigned)(bottom-top)/8,bytes=cols*rows*64;
    if(bytes>5120) panic(0xb1);
    g_hw_weapon[0]=(unsigned)left;g_hw_weapon[1]=(unsigned)top;
    g_hw_weapon[2]=cols;g_hw_weapon[3]=rows;g_hw_weapon[4]=bytes;
    dok_gba_tiled_clear(g_render_frame.frame+HW_WEAPON_STAGE,bytes);
    return 1;
}
static void hw_direct_ui_begin(void) {
    hw_direct_ui_stamp=timer_sample();
#ifndef DOK_RENDER_HW_UI_REUSE_PROTOTYPE
    dok_gba_tiled_clear(g_render_frame.frame+HW_UI_STAGE,HW_UI_BYTES);
#endif
}
#if !defined(DOK_RENDER_HW_UI_OBJ_PROTOTYPE) || defined(DOK_RENDER_HW_LOW_ADAPTIVE_PROTOTYPE)
static void hw_direct_map(volatile uint16_t *map) {
#ifdef DOK_RENDER_HW_MAP_CACHE_PROTOTYPE
    if(!hw_direct_map_dirty())return;
    hw_old_keys=g_render_world.player.keys;
#endif
    for(unsigned y=0;y<hw_old_weapon[3];++y) for(unsigned x=0;x<hw_old_weapon[2];++x)
        map[(hw_old_weapon[1]/8+y)*32+hw_old_weapon[0]/8+x]=HW_EMPTY_TILE;
    for(unsigned y=16;y<18;++y) for(unsigned x=24;x<30;++x) map[y*32+x]=HW_EMPTY_TILE;
    for(unsigned i=0;i<4;++i) hw_old_weapon[i]=g_hw_weapon[4]?g_hw_weapon[i]:0;
    if(g_hw_weapon[4]) for(unsigned y=0;y<g_hw_weapon[3];++y) for(unsigned x=0;x<g_hw_weapon[2];++x)
        map[(g_hw_weapon[1]/8+y)*32+g_hw_weapon[0]/8+x]=(uint16_t)(384+y*g_hw_weapon[2]+x);
    for(unsigned y=0;y<2;++y) {
        for(unsigned x=0;x<30;++x) map[(18+y)*32+x]=(uint16_t)(HW_UI_TILE_BASE+y*30+x);
        for(unsigned x=0;x<9;++x) map[y*32+x]=(uint16_t)(HW_UI_TILE_BASE+60+y*9+x);
        for(unsigned x=0;x<8;++x) map[y*32+22+x]=(uint16_t)(HW_UI_TILE_BASE+78+y*8+x);
    }
    unsigned keys=0;for(unsigned i=0;i<3;++i) if(g_render_world.player.keys&(1u<<i)) ++keys;
    for(unsigned y=0;y<2;++y) for(unsigned x=6-keys*2;x<6;++x)
        map[(16+y)*32+24+x]=(uint16_t)(HW_UI_TILE_BASE+94+y*6+x);
}
#endif
