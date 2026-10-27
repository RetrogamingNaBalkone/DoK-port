/* Experimental ROM atlas of the exact original panel bevel at HUD sizes.
 * This function is injected only into isolated UI-cache prototype builds. */
static int cached_panel(canvas *c,unsigned x,unsigned y,unsigned w,unsigned h) {
    extern const uint8_t dok_ui_panel_bank[];
    extern const uint8_t *const dok_ui_panel_sources[2];
    extern const uint32_t dok_ui_panel_offsets[2][14];
    extern void dok_gba_copy_panel_rows(uint8_t *,const uint8_t *,unsigned,unsigned);
    unsigned index;
    if(w==240 && h==16) index=0;
    else if(w==16 && h==16) index=1;
    else if(h==14 && w>=24 && w<=68 && !(w&3)) index=2+(w-24)/4;
    else return 0;
    if(!art || !art->panel.pixels || (x&3) || x+w>240 || y+h>160) return 0;
    unsigned language;
    if(art->panel.pixels==dok_ui_panel_sources[0]) language=0;
    else if(art->panel.pixels==dok_ui_panel_sources[1]) language=1;
    else return 0;
#ifdef DOK_RENDER_HW_DIRECT_PROTOTYPE
    if(g_hw_tile_active) {
        extern const uint8_t dok_hw_ui_panel_bank[];
        extern const uint32_t dok_hw_ui_panel_offsets[2][14];
        extern void dok_gba_tiled_ui_panel(uint8_t *,const uint8_t *,unsigned,unsigned);
        unsigned pitch;uint8_t *dst=ui_tiled_pointer(c->f,x,y,&pitch);
        if(!dst || (x&7) || (y&7))return 0;
        dok_gba_tiled_ui_panel(dst,dok_hw_ui_panel_bank+dok_hw_ui_panel_offsets[language][index],(w+7)/8,pitch);return 1;
    }
#endif
    dok_gba_copy_panel_rows(c->f+y*240+x,dok_ui_panel_bank+dok_ui_panel_offsets[language][index],w/4,h);
    return 1;
}
