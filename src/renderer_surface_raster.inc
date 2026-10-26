/* Horizontal spans have no transparency branch in the native ASM. The
 * production port shares front-to-back coverage with vertical planes. */
static int surface_covered(dok_renderer_workspace *w,unsigned x,unsigned y) {
    for(unsigned k=0;k<COVER_COUNT(w,x);++k) {
        if(COVER(w,x,k)[0]>y) return 0;
        if(COVER(w,x,k)[1]>=y) return 1;
    }
    return 0;
}
static void surface_raster(dok_renderer_workspace *w,const dok_runtime *r,const dok_renderer_surface *f,dok_renderer_stats *stats) {
    const dok_image_view *im=&w->surface_images[f->object][f->plane];
    if(f->plane==2) {
        dok_renderer_image saved=w->images[f->object];
        dok_renderer_image image={im->pixels,0,0,im->width,im->height,im->stored_flags,0};
        dok_renderer_face face={f->x[0],f->x[1],f->y[0],f->y[1],f->y[3],f->y[2],
            f->depth[0],f->depth[1],f->u0,f->u1,f->key,f->object,f->v0};
        unsigned drawn=stats->faces_drawn;
        w->images[f->object]=image;raster(w,r,&face,w->lights,stats);stats->faces_drawn=drawn;
        w->images[f->object]=saved;return;
    }
    int shift_x=0,shift_y=0,wrap=0;
    const dok_runtime_definition *definition=&r->level->definitions[f->object];
    if(!f->plane && (definition->class_id==12 || definition->class_id==13)) {
        const dok_runtime_object *object=&r->objects[f->object];
        unsigned flags=definition->sprite_flags,phase=(object->known&DOK_RUNTIME_CROP_PHASE)?
            (uint16_t)object->animation_a%18u:object->frame%18u;
        int dx=(im->width/18)*(int)phase,dy=(im->height/18)*(int)phase;
        if(flags&0x440) {shift_x=-dx;wrap=(flags&0x400)!=0;}
        else if(flags&0x880) {shift_x=dx;wrap=(flags&0x800)!=0;}
        else if(flags&0x1100) {shift_y=-dy;wrap=(flags&0x1000)!=0;}
        else if(flags&0x2200) {shift_y=dy;wrap=(flags&0x2000)!=0;}
    }
    int min=0,max=0;
    for(int k=1;k<4;++k) {if(f->y[k]<f->y[min]) min=k;if(f->y[k]>f->y[max]) max=k;}
    int yfirst=clamp(f->y[min],0,143),ylast=clamp(f->y[max],0,143);
    int u[4]={0,im->width?im->width-1:0,im->width?im->width-1:0,0};
    int v[4]={0,0,im->height?im->height-1:0,im->height?im->height-1:0};
    int d0=f->depth[min],d1=f->depth[max];if(d0<0) d0=-d0;if(d1<0) d1=-d1;
    int xmin=f->x[0],xmax=xmin;
    for(int k=1;k<4;++k) {if(f->x[k]<xmin) xmin=f->x[k];if(f->x[k]>xmax) xmax=f->x[k];}
    int hidden=1;
    for(int x=clamp(xmin,0,239);x<=clamp(xmax,0,239);++x) {
#ifdef DOK_RENDER_HALF_COLUMNS
        if(x&1) continue;
#endif
        int found=0;
        for(unsigned k=0;k<COVER_COUNT(w,x);++k)
            if(COVER(w,x,k)[0]<=yfirst && COVER(w,x,k)[1]>=ylast) {found=1;break;}
        if(!found) {hidden=0;break;}
    }
    if(hidden) return;
    struct {int first,last;int32_t x,u,v,dx,du,dv;} edges[4];
    for(int k=0;k<4;++k) {
        int a=k,b=(k+1)&3;if(f->y[a]>f->y[b]) {int n=a;a=b;b=n;}
        int span=f->y[b]-f->y[a];edges[k].first=f->y[a];edges[k].last=f->y[b];
        edges[k].dx=span?(f->x[b]-f->x[a])*65536/span:0;
        edges[k].du=span?(u[b]-u[a])*65536/span:0;
        edges[k].dv=span?(v[b]-v[a])*65536/span:0;
        edges[k].x=add(f->x[a]*65536,mul(yfirst-f->y[a],edges[k].dx));
        edges[k].u=add(u[a]*65536,mul(yfirst-f->y[a],edges[k].du));
        edges[k].v=add(v[a]*65536,mul(yfirst-f->y[a],edges[k].dv));
    }
    int depth_step=(d1-d0)*65536/(f->y[max]-f->y[min]);
    int depth=d0*65536+(yfirst-f->y[min])*depth_step;
    for(int y=yfirst;y<=ylast;++y) {
        int32_t xs[4],us[4],vs[4];int count=0;
        for(int k=0;k<4;++k) {
            if(edges[k].first!=edges[k].last && y>=edges[k].first && y<=edges[k].last) {
                xs[count]=edges[k].x;us[count]=edges[k].u;vs[count]=edges[k].v;++count;
            }
            edges[k].x=add(edges[k].x,edges[k].dx);
            edges[k].u=add(edges[k].u,edges[k].du);edges[k].v=add(edges[k].v,edges[k].dv);
        }
        unsigned row=light_index(sar(depth,16));depth=add(depth,depth_step);
        if(count<2) continue;
        int lo=0,hi=0;for(int k=1;k<count;++k) {if(xs[k]<xs[lo]) lo=k;if(xs[k]>xs[hi]) hi=k;}
        int left=sar(xs[lo],16),right=sar(xs[hi],16),span=right-left;
        if(span<=0 || right<0 || left>239) continue;
        int first=clamp(left,0,239),last=clamp(right,0,239);
#ifdef DOK_RENDER_HALF_COLUMNS
        first=(first+1)&~1;
#endif
        const uint8_t *lut=w->lights+row*256;
        int32_t du=(us[hi]-us[lo])/span,dv=(vs[hi]-vs[lo])/span;
        int32_t tu=us[lo]+(first-left)*du,tv=vs[lo]+(first-left)*dv;
        for(int x=first;x<=last;
#ifdef DOK_RENDER_HALF_COLUMNS
            x+=2,tu+=du*2,tv+=dv*2
#else
            ++x,tu+=du,tv+=dv
#endif
        ) {
            if(surface_covered(w,(unsigned)x,(unsigned)y)) continue;
            uint8_t color;
            {
                int tx=clamp(sar(tu,16),0,im->width-1),ty=clamp(sar(tv,16),0,im->height-1);
                tx+=shift_x;ty+=shift_y;
                if(wrap) {
                    if(tx<0) tx+=im->width;else if(tx>=im->width) tx-=im->width;
                    if(ty<0) ty+=im->height;else if(ty>=im->height) ty-=im->height;
                }
                color=(tx<0 || tx>=im->width || ty<0 || ty>=im->height)?lut[0]:lut[im->pixels[(unsigned)ty*im->width+(unsigned)tx]];
            }
            store_pixel(w->frame+y*240+x,color);
            unsigned covered=COVER_COUNT(w,x);
            if(!covered) {
                COVER(w,x,0)[0]=COVER(w,x,0)[1]=(uint8_t)y;COVER_COUNT(w,x)=1;
            } else if(covered==1 && (unsigned)y+1>=COVER(w,x,0)[0] && (unsigned)y<=(unsigned)COVER(w,x,0)[1]+1) {
                if(y<COVER(w,x,0)[0]) COVER(w,x,0)[0]=(uint8_t)y;
                if(y>COVER(w,x,0)[1]) COVER(w,x,0)[1]=(uint8_t)y;
            } else cover(w,(unsigned)x,(unsigned)y,(unsigned)y);
            ++stats->pixels_written;
        }
    }
}
