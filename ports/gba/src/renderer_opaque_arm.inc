typedef struct {
    uint8_t *frame;const uint8_t *bank;const uint32_t *rows;uint32_t *bits;uint8_t *counts;
    int x,last,denom,numerator,dd,nd;unsigned span,vbase,height,width;
    int top,bottom,depth;edge_walk upper,lower,z;unsigned black;
} opaque_tiled_face;
volatile uint32_t g_opaque_work[4]; /* fast faces,fallback faces,fast pixels,fallback reasons */
_Static_assert(offsetof(opaque_tiled_face,upper)==72 && offsetof(opaque_tiled_face,black)==144,"opaque ARM capsule ABI");
extern unsigned dok_gba_opaque_tiled(opaque_tiled_face *);
#ifdef DOK_RENDER_OPAQUE_GAPS_PROTOTYPE
static DOK_NOINLINE unsigned raster_opaque_arm(dok_renderer_workspace *w,const dok_renderer_face *source,const dok_renderer_image *im,const uint32_t *rows) {
#else
static DOK_COLD unsigned raster_opaque_arm(dok_renderer_workspace *w,const dok_renderer_face *source,const dok_renderer_image *im,const uint32_t *rows) {
#endif
    extern const uint8_t dok_prelit_bank[];dok_renderer_face f=*source;
    if(f.x0>f.x1){swap32(&f.x0,&f.x1);swap32(&f.top0,&f.top1);swap32(&f.bottom0,&f.bottom1);swap32(&f.depth0,&f.depth1);swap32(&f.u0,&f.u1);}
    int span=f.x1-f.x0,first=(clamp(f.x0,0,239)+1)&~1,last=clamp(f.x1,0,239);
    if(span<=0 || f.x1<0 || f.x0>239 || first>last)return 0;
    int q0=(6553600/(f.depth0+200))>>8,q1=(6553600/(f.depth1+200))>>8;
    if(q0<1)q0=1;if(q1<1)q1=1;int i=first-f.x0;
    edge_walk edges[3]={edge_start(f.top1-f.top0,(unsigned)i,(unsigned)span),edge_start(f.bottom1-f.bottom0,(unsigned)i,(unsigned)span),edge_start(f.depth1-f.depth0,(unsigned)i,(unsigned)span)};
    for(unsigned n=0;n<3;++n) {unsigned twice=edges[n].step*2+(edges[n].carry*2)/(unsigned)span;edges[n].carry=(edges[n].carry*2)%(unsigned)span;edges[n].step=twice;}
    opaque_tiled_face capsule={w->frame,dok_prelit_bank,rows,&coverage_bits[(unsigned)first>>1][0],&coverage_count[(unsigned)first>>1],
        first,last,q0*(span-i)+q1*i,f.u0*q0*(span-i)+f.u1*q1*i,(q1-q0)*2,(f.u1*q1-f.u0*q0)*2,(unsigned)span,
        (unsigned)clamp(f.v0,0,im->height-1),im->height,im->width,f.top0,f.bottom0,f.depth0,edges[0],edges[1],edges[2],g_hw_tile_black};
    return dok_gba_opaque_tiled(&capsule);
}
#ifdef DOK_RENDER_OPAQUE_GAPS_PROTOTYPE
static DOK_COLD void raster(dok_renderer_workspace *w,const dok_runtime *r,const dok_renderer_face *f,const uint8_t *light,dok_renderer_stats *stats,int vend) {
#else
static void raster(dok_renderer_workspace *w,const dok_runtime *r,const dok_renderer_face *f,const uint8_t *light,dok_renderer_stats *stats,int vend) {
#endif
    if(vend>=0) {raster_general(w,r,f,light,stats,vend);return;}
    const dok_renderer_image *im=face_image(w,f->image_index);
    if(g_hw_tile_active && !(im->flags&8) && im->columns && im->resource_id &&
        f->depth0>=0 && f->depth1>=0 && f->depth0<32768 && f->depth1<32768 &&
        !(f->image_index<r->object_count && r->level->definitions[f->image_index].class_id==7)) {
        const uint32_t *rows=prelit_view+im->resource_id*16;
        if(rows[0]){++g_opaque_work[0];unsigned n=raster_opaque_arm(w,f,im,rows);g_opaque_work[2]+=n;stats->pixels_written+=n;if(n)++stats->faces_drawn;return;}
    }
    ++g_opaque_work[1];g_opaque_work[3]|=(im->flags&8?1u:0u)|(!im->columns?2u:0u)|(!im->resource_id?4u:0u)|
        (f->depth0<0 || f->depth1<0 || f->depth0>=32768 || f->depth1>=32768?8u:0u);
    raster_general(w,r,f,light,stats,vend);
}
