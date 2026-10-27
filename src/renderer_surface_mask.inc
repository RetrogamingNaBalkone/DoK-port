/* The second affine background supplies inset400 texels. Retain the native
 * projected polygon and front-to-back coverage, including its edge rounding. */
static SURFACE_RASTER_ATTR void SURFACE_RASTER_NAME(dok_renderer_workspace *w,const dok_runtime *r,
    const dok_renderer_surface *f,dok_renderer_stats *stats) {
    (void)w;(void)r;
#ifdef DOK_RENDER_HW_ROWS96_PROTOTYPE
    enum {mask_rows=96};
#else
    enum {mask_rows=128};
#endif
    extern unsigned dok_gba_coverage_span(uint32_t *,unsigned,unsigned);
    int ymin=f->y[0],ymax=ymin,xmin=f->x[0],xmax=xmin;
    for(unsigned k=1;k<4;++k) {
        if(f->y[k]<ymin)ymin=f->y[k];if(f->y[k]>ymax)ymax=f->y[k];
        if(f->x[k]<xmin)xmin=f->x[k];if(f->x[k]>xmax)xmax=f->x[k];
    }
    int first_y=clamp(ymin,0,mask_rows-1),last_y=clamp(ymax,0,mask_rows-1);
    if(xmax<0 || xmin>239 || ymax<0 || ymin>=mask_rows)return;
    for(int x=(clamp(xmin,0,239)+1)&~1;x<=clamp(xmax,0,239);x+=2)COVER_COUNT(w,x)|=128;
    struct {int first,last;int32_t x,dx;} edges[4];
    for(unsigned k=0;k<4;++k) {
        unsigned a=k,b=(k+1)&3;
        if(f->y[a]>f->y[b]){unsigned t=a;a=b;b=t;}
        int span=f->y[b]-f->y[a];edges[k].first=f->y[a];edges[k].last=f->y[b];
        edges[k].dx=span?PLANE_DIV((f->x[b]-f->x[a])*65536,(unsigned)span):0;
        edges[k].x=add(f->x[a]*65536,mul(first_y-f->y[a],edges[k].dx));
    }
    for(int y=first_y;y<=last_y;++y) {
        int32_t left=INT_MAX,right=INT_MIN;unsigned count=0;
        for(unsigned k=0;k<4;++k) {
            if(edges[k].first!=edges[k].last && y>=edges[k].first && y<=edges[k].last) {
                int32_t x=add(edges[k].x,mul(y-first_y,edges[k].dx));
                if(x<left)left=x;if(x>right)right=x;++count;
            }
        }
        if(count<2)continue;
        left=sar(left,16);right=sar(right,16);
        if(right<=left || right<0 || left>239)continue;
        unsigned first=(unsigned)(clamp(left,0,239)+1)&~1u,last=(unsigned)clamp(right,0,239);
        if(first>last)continue;
        stats->pixels_written+=dok_gba_coverage_span(&coverage_bits[first>>1][(unsigned)y>>5],1u<<((unsigned)y&31),((last-first)>>1)+1);
    }
}
