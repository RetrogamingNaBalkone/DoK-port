/* Lossless constant-depth rectangle path. Other faces retain the general
 * perspective rasterizer, including moving/cropped doors and animation. */
static DOK_NOINLINE unsigned raster_rect(dok_renderer_workspace *w,const dok_renderer_face *f,
    const dok_renderer_image *im,dok_renderer_stats *s) {
#ifdef DOK_RENDER_HW_ROWS96_PROTOTYPE
    enum {rect_rows=96};
#elif defined(DOK_RENDER_HW_ROWS72_PROTOTYPE)
    enum {rect_rows=72};
#elif defined(DOK_RENDER_HW_VERTICAL128_PROTOTYPE)
    enum {rect_rows=128};
#else
    enum {rect_rows=DOK_RENDER_SCENE_HEIGHT};
#endif
    extern const uint8_t dok_prelit_bank[];
    if(f->depth0!=f->depth1 || f->depth0<0 || f->top0!=f->top1 || f->bottom0!=f->bottom1 ||
       f->u0<0 || f->u1<0 || f->u0>255 || f->u1>255 || !im->columns || !im->resource_id)return 0;
    unsigned prelit=prelit_view[im->resource_id*16+light_index(f->depth0)];
    if(!prelit)return 0;
    int span=f->x1-f->x0;
    if(span<=0 || span>16384)return 0;
    int top=f->top0,bottom=f->bottom0;
    if(bottom<top || bottom<0 || top>=rect_rows)return 1;
    unsigned begin=(unsigned)clamp(top,0,rect_rows-1),end=(unsigned)clamp(bottom,0,rect_rows-1);
#ifdef DOK_RENDER_STREAM_BSP_PROTOTYPE
    if(stream_active && stream_vertical_clip) {
        int factor=6553600/(f->depth0+200);
        int limit=scale_y(sar(mul(stream_ceiling-stream_camera->y,factor),15)+79);
        if(limit>(int)begin)begin=(unsigned)limit;
        if(begin>end)return 1;
    }
#endif
    int vstart=clamp(f->v0,0,im->height-1);
    unsigned step=bottom>top?(unsigned)VSTEP_DIV((im->height-1-vstart)<<16,bottom-top):0;
    unsigned v=((unsigned)vstart<<16)+(begin-top)*step;
    int first=(clamp(f->x0,0,239)+1)&~1,last=clamp(f->x1,0,239);
#ifdef DOK_RENDER_STREAM_BSP_PROTOTYPE
    if(stream_active) {
        if(first<(int)stream_clip_first)first=(int)((stream_clip_first+1)&~1u);
        if(last>(int)stream_clip_last)last=(int)stream_clip_last;
    }
#endif
    unsigned pixels=0,transparent=0;
    for(int x=first;x<=last;x+=2) {
        if(
#ifdef DOK_RENDER_SECTOR_CLIP_PROTOTYPE
            !sector_dynamic &&
#endif
            coverage_contains((unsigned)x,begin,end))continue;
        int i=x-f->x0;
        unsigned u=(unsigned)((f->u0*(span-i)+f->u1*i)/span);
        if(u>=im->width)u=im->width-1;
        unsigned mode=PRELIT_COLUMN_MODE(im->flags,prelit,u);
        unsigned n=visible_column(w,(unsigned)x,begin,end,dok_prelit_bank+prelit+u*im->height,v,step,mode,0,0);
        transparent+=n>>16;pixels+=n&65535u;
    }
    s->pixels_written+=pixels;s->transparent_pixels+=transparent;
    if(pixels)++s->faces_drawn;
    return 1;
}
