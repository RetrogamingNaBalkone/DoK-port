/* Specialized exact vertical raster for prelit opaque images. The general
 * path remains authoritative for doors, alpha textures and animated actors. */
static DOK_NOINLINE void raster_opaque(dok_renderer_workspace *w,const dok_renderer_face *source,
    const dok_renderer_image *im,const uint32_t *rows,dok_renderer_stats *stats) {
    extern const uint8_t dok_prelit_bank[];
    dok_renderer_face f=*source;
    if(f.x0>f.x1){swap32(&f.x0,&f.x1);swap32(&f.top0,&f.top1);swap32(&f.bottom0,&f.bottom1);swap32(&f.depth0,&f.depth1);swap32(&f.u0,&f.u1);}
    int span=f.x1-f.x0,first=(clamp(f.x0,0,239)+1)&~1,last=clamp(f.x1,0,239);
    if(span<=0 || f.x1<0 || f.x0>239 || first>last)return;
    int q0=(6553600/(f.depth0+200))>>8,q1=(6553600/(f.depth1+200))>>8;
    if(q0<1)q0=1;if(q1<1)q1=1;
    unsigned width=im->width,height=im->height,vbase=(unsigned)clamp(f.v0,0,(int)height-1);
    edge_walk upper=edge_start(f.top1-f.top0,(unsigned)(first-f.x0),(unsigned)span);
    edge_walk lower=edge_start(f.bottom1-f.bottom0,(unsigned)(first-f.x0),(unsigned)span);
    edge_walk depth=edge_start(f.depth1-f.depth0,(unsigned)(first-f.x0),(unsigned)span);
    int i=first-f.x0,denom=q0*(span-i)+q1*i,numerator=f.u0*q0*(span-i)+f.u1*q1*i;
    int dd=(q1-q0)*2,nd=(f.u1*q1-f.u0*q0)*2;unsigned written=0;
    for(int x=first;x<=last;x+=2,denom+=dd,numerator+=nd) {
        int top=f.top0+upper.sign*(int)upper.quotient,bottom=f.bottom0+lower.sign*(int)lower.quotient;
        int d=f.depth0+depth.sign*(int)depth.quotient;
        edge_next(&upper);edge_next(&upper);edge_next(&lower);edge_next(&lower);edge_next(&depth);edge_next(&depth);
        if(bottom<top || bottom<0 || top>=144)continue;
        unsigned begin=(unsigned)clamp(top,0,143),end=(unsigned)clamp(bottom,0,143);
        if(coverage_contains((unsigned)x,begin,end))continue;
        coverage_sync(w,(unsigned)x);
        int u=denom?numerator/denom:0;u=clamp(u,0,(int)width-1);
        unsigned step=bottom>top?((height-1-vbase)<<16)/(unsigned)(bottom-top):0;
        unsigned v=(vbase<<16)+(begin-(unsigned)top)*step,row=light_index(d);
        written+=visible_column(w,(unsigned)x,begin,end,dok_prelit_bank+rows[row]+(unsigned)u*height,v,step,0,0,0)&65535u;
    }
    stats->pixels_written+=written;if(written)++stats->faces_drawn;
}
static void raster(dok_renderer_workspace *w,const dok_runtime *r,const dok_renderer_face *f,const uint8_t *light,dok_renderer_stats *stats,int vend) {
    if(vend>=0) {raster_general(w,r,f,light,stats,vend);return;}
    const dok_renderer_image *im=face_image(w,f->image_index);
    if(!(im->flags&8) && im->columns && im->resource_id &&
        !(f->image_index<r->object_count && r->level->definitions[f->image_index].class_id==7)) {
        const uint32_t *rows=prelit_view+im->resource_id*16;
        if(rows[0]){raster_opaque(w,f,im,rows,stats);return;}
    }
    raster_general(w,r,f,light,stats,vend);
}
