/* Lossless constant-depth rectangle path. Other faces retain the general
 * perspective rasterizer, including moving/cropped doors and animation. */
#ifdef DOK_RENDER_HW_RECT_BATCH_PROTOTYPE
typedef struct {
    uint8_t *dst;const uint8_t *pixels,*solid;uint32_t *bits;uint8_t *counts;
    unsigned x,last,width,height,numerator;int delta;
    unsigned reciprocal,span,v,step,count,begin,black,masked;
    unsigned first_word,last_word,first_mask,last_mask,bit_stride;
} rect_batch;
_Static_assert(sizeof(rect_batch)==96 && offsetof(rect_batch,bit_stride)==92,"ARM rectangle capsule ABI");
extern unsigned dok_gba_tiled_rectangle(rect_batch *);
static DOK_COLD unsigned raster_rect(dok_renderer_workspace *w,const dok_renderer_face *f,
#else
static DOK_NOINLINE unsigned raster_rect(dok_renderer_workspace *w,const dok_renderer_face *f,
#endif
    const dok_renderer_image *im,dok_renderer_stats *s,int vend) {
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
#ifdef DOK_RENDER_HW_SPARSE_POSTS_PROTOTYPE
    unsigned row=light_index(f->depth0);
    unsigned prelit=prelit_view[im->resource_id*16+row];
#else
    unsigned prelit=prelit_view[im->resource_id*16+light_index(f->depth0)];
#endif
    const uint8_t *source,*solid;
    if(prelit){source=dok_prelit_bank+prelit;solid=source-256;}
#ifdef DOK_RENDER_HW_ANIMATION_COLUMNS_PROTOTYPE
    else if(!light_index(f->depth0)){source=im->columns;solid=source-256;}
#endif
    else return 0;
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
    /* Caller supplies a bounded end, or -1 for the whole image. */
    if(vend<0)vend=im->height-1;
    int vstart=clamp(f->v0,0,vend);
    unsigned step=bottom>top?(unsigned)VSTEP_DIV((vend-vstart)<<16,bottom-top):0;
    unsigned v=((unsigned)vstart<<16)+(begin-top)*step;
    int first=(clamp(f->x0,0,239)+1)&~1,last=clamp(f->x1,0,239);
#ifdef DOK_RENDER_STREAM_BSP_PROTOTYPE
    if(stream_active) {
        if(first<(int)stream_clip_first)first=(int)((stream_clip_first+1)&~1u);
        if(last>(int)stream_clip_last)last=(int)stream_clip_last;
    }
#endif
    unsigned pixels=0,transparent=0;
#ifdef DOK_RENDER_HW_RECT_BATCH_PROTOTYPE
    if(first>last || begin>end)return 1;
    extern const uint32_t dok_exact_plane_reciprocal[];
    rect_batch batch={scene_pointer(w->frame,(unsigned)first,begin),source,
        solid,&coverage_bits[(unsigned)first>>1][0],&coverage_count[(unsigned)first>>1],
        (unsigned)first,(unsigned)last,im->width,im->height,
        (unsigned)(f->u0*(span-(first-f->x0))+f->u1*(first-f->x0)),(f->u1-f->u0)*2,
        dok_exact_plane_reciprocal[span],(unsigned)span,v,step,end-begin+1,begin,g_hw_tile_black,im->flags&8u,
        begin>>5,end>>5,UINT32_MAX<<(begin&31),UINT32_MAX>>(31-(end&31)),sizeof(coverage_bits[0])};
    unsigned result=dok_gba_tiled_rectangle(&batch);pixels=result&65535u;transparent=result>>16;
#else
    for(int x=first;x<=last;x+=2) {
        if(
#ifdef DOK_RENDER_SECTOR_CLIP_PROTOTYPE
            !sector_dynamic &&
#endif
            coverage_contains((unsigned)x,begin,end))continue;
        int i=x-f->x0;
        unsigned u=(unsigned)((f->u0*(span-i)+f->u1*i)/span);
        if(u>=im->width)u=im->width-1;
        unsigned mode=prelit?PRELIT_COLUMN_MODE(im->flags,prelit,u):COLUMN_MODE(im->flags,solid[u]);
#ifdef DOK_RENDER_HW_SPARSE_POSTS_PROTOTYPE
        SET_COLUMN_POSTS(im,prelit,u);
#endif
        unsigned n=visible_column(w,(unsigned)x,begin,end,source+u*im->height,v,step,mode,0,0);
        transparent+=n>>16;pixels+=n&65535u;
    }
#endif
    s->pixels_written+=pixels;s->transparent_pixels+=transparent;
    if(pixels)++s->faces_drawn;
    return 1;
}
