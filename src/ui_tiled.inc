/* Private Mode1 HUD atlas. Coordinates keep the public240x160 UI contract. */
extern uint32_t g_hw_tile_active;
static uint8_t *ui_tiled_pointer(uint8_t *f,unsigned x,unsigned y,unsigned *pitch) {
    unsigned tile;
    if(y>=144) {y-=144;tile=(y/8)*30+x/8;if(pitch)*pitch=30*64;}
    else if(y<16 && x<72) {tile=60+(y/8)*9+x/8;if(pitch)*pitch=9*64;}
    else if(y<16 && x>=176) {x-=176;tile=78+(y/8)*8+x/8;if(pitch)*pitch=8*64;}
    else if(y>=128 && x>=192) {y-=128;x-=192;tile=94+(y/8)*6+x/8;if(pitch)*pitch=6*64;}
    else return 0;
    return f+18432+tile*64+(y&7)*8+(x&7);
}
static void ui_tiled_put(canvas *c,unsigned x,unsigned y,unsigned color) {
    if(g_hw_tile_active) {uint8_t *p=ui_tiled_pointer(c->f,x,y,0);if(p)*p=(uint8_t)color;}
    else c->f[y*240+x]=(uint8_t)color;
}
#define UI_PUT(c,x,y,color) ui_tiled_put((c),(x),(y),(color))
