/* Cost estimate only: never removes geometry or reduces consideration.
 * A coarse screen grid estimates potentially visible ceiling work after
 * conservative opaque rectangles. The selected paths share geometry/coverage. */
uint32_t g_hw_low_mode;
volatile uint32_t g_hw_low_choice[4]; /* visible tiles,ceil polygons,switches,mode */
#ifdef DOK_RENDER_HW_LOW_FEEDBACK_PROTOTYPE
unsigned g_hw_low_samples;
volatile unsigned g_hw_low_force; /* Test mailbox:0 auto,1 CPU,2 BG3. */
#endif
static DOK_COLD void hw_low_choose(const dok_renderer_workspace *w,const dok_runtime *r) {
#ifdef DOK_RENDER_HW_LOW_FEEDBACK_PROTOTYPE
    (void)w;(void)r;
    unsigned visible=g_hw_low_samples;
    /* Forced paired measurements put the observed break-even above895 and
     * below3508 samples. Keep margin plus hysteresis; no map/camera IDs. */
    unsigned mode=g_hw_tile_active && visible>=(g_hw_low_mode?1400u:2000u);
    if(g_hw_tile_active && g_hw_low_force)mode=g_hw_low_force==2;
    if(mode!=g_hw_low_mode)++g_hw_low_choice[2];
    g_hw_low_mode=mode;g_hw_low_choice[0]=visible;g_hw_low_choice[1]=0;g_hw_low_choice[3]=mode;
    g_hw_low_samples=0;
#else
    unsigned ceilings=0;
    for(unsigned i=0;i<w->surface_count;++i)ceilings+=w->surfaces[i].plane==1;
    unsigned visible=0,mode=0;
    if(g_hw_tile_active && ceilings) {
#ifdef DOK_RENDER_HW_LOW_GRID4_PROTOTYPE
        enum {grid_side=4};
        int32_t nearest[16];uint16_t candidates[4]={0};
        for(unsigned i=0;i<16;++i)nearest[i]=INT_MAX;
#else
        enum {grid_side=16};
        int32_t nearest[256];uint16_t candidates[16]={0};
        for(unsigned i=0;i<256;++i)nearest[i]=INT_MAX;
#endif
        for(unsigned i=0;i<w->face_count;++i) {
            const dok_renderer_face *f=&w->faces[i];
            if(face_image(w,f->image_index)->flags&8u)continue;
            if(f->image_index<r->object_count && r->level->definitions[f->image_index].class_id==7)continue;
            int left=f->x0<f->x1?f->x0:f->x1,right=f->x0>f->x1?f->x0:f->x1;
            int top=f->top0>f->top1?f->top0:f->top1,bottom=f->bottom0<f->bottom1?f->bottom0:f->bottom1;
            if(left<0)left=0;if(top<0)top=0;if(right>239)right=239;if(bottom>127)bottom=127;
#ifdef DOK_RENDER_HW_LOW_GRID4_PROTOTYPE
            /* Sample centers instead of filling256 visibility cells. Only a
             * cost estimate: geometry/coverage remain on their original paths. */
            if(right<left || bottom<top || right<30 || bottom<16)continue;
            int first_x=(left+29)/60,last_x=(right-30)/60;
            int first_y=(top+15)/32,last_y=(bottom-16)/32;
#else
            int first_x=(left+14)/15,last_x=(right-14)/15;
            int first_y=(top+7)/8,last_y=(bottom-7)/8;
            if(right<left || bottom<top || right<14 || bottom<7)continue;
#endif
            for(int y=first_y;y<=last_y;++y)for(int x=first_x;x<=last_x;++x)
                if(nearest[y*grid_side+x]>f->key)nearest[y*grid_side+x]=f->key;
        }
        for(unsigned i=0;i<w->surface_count;++i) {
            const dok_renderer_surface *f=&w->surfaces[i];if(f->plane!=1)continue;
            int left=f->x[0],right=left,top=f->y[0],bottom=top;
            for(unsigned k=1;k<4;++k) {
                if(f->x[k]<left)left=f->x[k];if(f->x[k]>right)right=f->x[k];
                if(f->y[k]<top)top=f->y[k];if(f->y[k]>bottom)bottom=f->y[k];
            }
            if(right<0 || left>239 || bottom<0 || top>127)continue;
            left=clamp(left,0,239);right=clamp(right,0,239);top=clamp(top,0,127);bottom=clamp(bottom,0,127);
#ifdef DOK_RENDER_HW_LOW_GRID4_PROTOTYPE
            if(right<30 || bottom<16)continue;
            for(int y=(top+15)/32;y<=(bottom-16)/32;++y)
                for(int x=(left+29)/60;x<=(right-30)/60;++x)
#else
            for(int y=top/8;y<=bottom/8;++y)for(int x=left/15;x<=right/15;++x)
#endif
                if(nearest[y*grid_side+x]>=f->key)candidates[y]|=(uint16_t)(1u<<x);
        }
        for(unsigned y=0;y<grid_side;++y) {
            unsigned bits=candidates[y];bits=bits-((bits>>1)&0x5555u);
            bits=(bits&0x3333u)+((bits>>2)&0x3333u);bits=(bits+(bits>>4))&0x0f0fu;
            visible+=(bits+(bits>>8))&31u;
        }
#ifdef DOK_RENDER_HW_LOW_GRID4_PROTOTYPE
        mode=visible>=5;
#else
        mode=visible>=80;
#endif
        /* About4800 logical samples; no fixture-specific data. */
    }
    if(mode!=g_hw_low_mode)++g_hw_low_choice[2];
    g_hw_low_mode=mode;g_hw_low_choice[0]=visible;g_hw_low_choice[1]=ceilings;g_hw_low_choice[3]=mode;
#endif
}
