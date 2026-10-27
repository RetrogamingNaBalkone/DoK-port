/* Shared scanline body: ordinary fallback and specialized hardware ceiling. */
static SURFACE_RASTER_ATTR void SURFACE_RASTER_NAME(dok_renderer_workspace *w,const dok_runtime *r,const dok_renderer_surface *f,dok_renderer_stats *stats) {
    const dok_image_view *im=&w->surface_images[f->object][SURFACE_RASTER_PLANE];
#if defined(DOK_RENDER_HW_LOW_ADAPTIVE_PROTOTYPE) && !defined(DOK_RENDER_HW_LOW_FEEDBACK_PROTOTYPE)
    extern uint32_t g_hw_low_mode;
    const unsigned mask_only=SURFACE_RASTER_TILED && SURFACE_RASTER_PLANE==1 && g_hw_low_mode;
#else
    const unsigned mask_only=0;
#endif
    if(SURFACE_RASTER_PLANE==2) {
        dok_renderer_image saved=w->images[f->object];
        dok_renderer_image image={im->pixels,0,0,im->width,im->height,im->stored_flags,0};
#ifdef DOK_RENDER_SIDES_PRELIT_PROTOTYPE
        extern const uint16_t dok_side_resources[6][256];
        extern const uint8_t dok_prelit_bank[];
        image.resource_id=dok_side_resources[static_view][f->object];
        if(image.resource_id) {
            uint32_t offset=prelit_view[image.resource_id*16];
            if(offset) image.columns=dok_prelit_bank+offset;
        }
#endif
        dok_renderer_face face={f->x[0],f->x[1],f->y[0],f->y[1],f->y[3],f->y[2],
            f->depth[0],f->depth[1],f->u0,f->u1,f->key,f->object,f->v0};
        unsigned drawn=stats->faces_drawn;
        w->images[f->object]=image;raster(w,r,&face,w->lights,stats);stats->faces_drawn=drawn;
        w->images[f->object]=saved;return;
    }
#ifndef DOK_RENDER_WORLD_PLANES_PROTOTYPE
    int shift_x=0,shift_y=0,wrap=0;
    const dok_runtime_definition *definition=&r->level->definitions[f->object];
    if(!SURFACE_RASTER_PLANE && (definition->class_id==12 || definition->class_id==13)) {
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
#ifdef DOK_RENDER_HW_CEILING2_PROTOTYPE
    int row_step=SURFACE_RASTER_TILED && SURFACE_RASTER_PLANE==1?2:1;
    if(row_step==2)yfirst=(yfirst+1)&~1;
#else
    const int row_step=1;
#endif
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
#if defined(DOK_RENDERER_PROBE) && defined(DOK_RENDER_HALF_COLUMNS)
        found=coverage_contains((unsigned)x,(unsigned)yfirst,(unsigned)ylast);
#else
        for(unsigned k=0;k<COVER_COUNT(w,x);++k)
            if(COVER(w,x,k)[0]<=yfirst && COVER(w,x,k)[1]>=ylast) {found=1;break;}
#endif
        if(!found) {hidden=0;break;}
    }
    if(hidden) return;
#if defined(DOK_RENDERER_PROBE) && defined(DOK_RENDER_HALF_COLUMNS)
    for(int x=(clamp(xmin,0,239)+1)&~1;x<=clamp(xmax,0,239);x+=2) COVER_COUNT(w,x)|=128;
#endif
    struct {int first,last;int32_t x,u,v,dx,du,dv;} edges[4];
    for(int k=0;k<4;++k) {
        int a=k,b=(k+1)&3;if(f->y[a]>f->y[b]) {int n=a;a=b;b=n;}
        int span=f->y[b]-f->y[a];edges[k].first=f->y[a];edges[k].last=f->y[b];
        edges[k].dx=span?PLANE_DIV((f->x[b]-f->x[a])*65536,span):0;
        edges[k].du=span && !mask_only?PLANE_DIV((u[b]-u[a])*65536,span):0;
        edges[k].dv=span && !mask_only?PLANE_DIV((v[b]-v[a])*65536,span):0;
        edges[k].x=add(f->x[a]*65536,mul(yfirst-f->y[a],edges[k].dx));
        edges[k].u=add(u[a]*65536,mul(yfirst-f->y[a],edges[k].du));
        edges[k].v=add(v[a]*65536,mul(yfirst-f->y[a],edges[k].dv));
    }
    int depth_step=mask_only?0:PLANE_DIV((d1-d0)*65536,(f->y[max]-f->y[min]));
    int depth=d0*65536+(yfirst-f->y[min])*depth_step;
#ifdef DOK_RENDER_SWEEP_PLANES_PROTOTYPE
    int active0=-1,active1=-1,active_until=-1;
#endif
    for(int y=yfirst;y<=ylast;y+=row_step) {
        int lo=-1,hi=-1,count=0;int32_t xl=0,xh=0;int offset=y-yfirst;
#ifdef DOK_RENDER_SWEEP_PLANES_PROTOTYPE
        if(y>active_until) {
            active0=active1=-1;active_until=INT_MAX;
            for(int k=0;k<4;++k) if(y>=edges[k].first && y<edges[k].last) {
                if(active0<0) active0=k;else active1=k;
                if(edges[k].last-1<active_until) active_until=edges[k].last-1;
            }
        }
        if(active0>=0 && active1>=0) {
            lo=active0;hi=active1;count=2;
            xl=add(edges[lo].x,mul(offset,edges[lo].dx));
            xh=add(edges[hi].x,mul(offset,edges[hi].dx));
            if(xl>xh) {int n=lo;lo=hi;hi=n;int32_t v=xl;xl=xh;xh=v;}
        }
#else
        for(int k=0;k<4;++k) {
            if(edges[k].first!=edges[k].last && y>=edges[k].first && y<=edges[k].last) {
                int32_t x=add(edges[k].x,mul(offset,edges[k].dx));
                if(lo<0 || x<xl) {lo=k;xl=x;}
                if(hi<0 || x>xh) {hi=k;xh=x;}
                ++count;
            }
        }
#endif
        unsigned row=mask_only?0:light_index(sar(depth,16));depth=add(depth,depth_step*row_step);
        if(count<2) continue;
        int left=sar(xl,16),right=sar(xh,16),span=right-left;
        if(span<=0 || right<0 || left>239) continue;
        int first=clamp(left,0,239),last=clamp(right,0,239);
#ifdef DOK_RENDER_HALF_COLUMNS
        first=(first+1)&~1;
#endif
        if(first>last) continue;
#if defined(DOK_RENDER_HW_LOW_ADAPTIVE_PROTOTYPE) && !defined(DOK_RENDER_HW_LOW_FEEDBACK_PROTOTYPE)
        if(mask_only) {
            extern unsigned dok_gba_coverage_span(uint32_t *,unsigned,unsigned);
            stats->pixels_written+=dok_gba_coverage_span(&coverage_bits[(unsigned)first>>1][(unsigned)y>>5],1u<<((unsigned)y&31),(unsigned)(last-first)/2+1);
            continue;
        }
#endif
#ifdef DOK_RENDER_HW_SURFACE_CLIP_PROTOTYPE
        if(SURFACE_RASTER_TILED && SURFACE_RASTER_PLANE==1) {
            uint32_t visible=surface_visible_range((unsigned)first,(unsigned)last,(unsigned)y);
            if(visible==UINT32_MAX)continue;
            first=(int)(visible&65535u);last=(int)(visible>>16);
        }
#endif
        const uint8_t *lut=
#ifdef DOK_RENDER_HW_SURFACE_PRELIT_PROTOTYPE
            SURFACE_RASTER_TILED?dok_ceiling_bank+dok_ceiling_offsets[static_view][row]:
#endif
            w->lights+row*256;
        int32_t ul=add(edges[lo].u,mul(offset,edges[lo].du)),uh=add(edges[hi].u,mul(offset,edges[hi].du));
        int32_t vl=add(edges[lo].v,mul(offset,edges[lo].dv)),vh=add(edges[hi].v,mul(offset,edges[hi].dv));
        int32_t du=PLANE_DIV(uh-ul,span),dv=PLANE_DIV(vh-vl,span);
        int32_t tu=ul+(first-left)*du,tv=vl+(first-left)*dv;
#if defined(DOK_RENDERER_PROBE) && defined(DOK_RENDER_HALF_COLUMNS)
        if(!shift_x && !shift_y) {
            surface_span s={im->pixels,lut,w->frame+y*240+first,im->width,im->height,
                (unsigned)first,(unsigned)last,(unsigned)y,tu,tv,du,dv,
                &coverage_bits[(unsigned)first>>1][(unsigned)y>>5],1u<<((unsigned)y&31)
#ifdef DOK_RENDER_HW_DIRECT_PROTOTYPE
                ,g_hw_tile_black
#endif
            };
#ifdef DOK_RENDER_HW_CEILING2_PROTOTYPE
            if(row_step==2)s.mask|=s.mask<<1;
#endif
#ifdef DOK_RENDER_HW_DIRECT_PROTOTYPE
            if(SURFACE_RASTER_TILED) {s.dst=scene_pointer(w->frame,(unsigned)first,(unsigned)y);stats->pixels_written+=dok_gba_tiled_surface(&s);continue;}
#endif
            stats->pixels_written+=dok_gba_surface_span(&s);continue;
        }
#endif
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
#if defined(DOK_RENDERER_PROBE) && defined(DOK_RENDER_HALF_COLUMNS)
            coverage_bits[(unsigned)x>>1][(unsigned)y>>5]|=1u<<((unsigned)y&31);
            COVER_COUNT(w,x)|=128;
#else
            unsigned covered=COVER_COUNT(w,x);
            if(!covered) {
                COVER(w,x,0)[0]=COVER(w,x,0)[1]=(uint8_t)y;COVER_COUNT(w,x)=1;
            } else if(covered==1 && (unsigned)y+1>=COVER(w,x,0)[0] && (unsigned)y<=(unsigned)COVER(w,x,0)[1]+1) {
                if(y<COVER(w,x,0)[0]) COVER(w,x,0)[0]=(uint8_t)y;
                if(y>COVER(w,x,0)[1]) COVER(w,x,0)[1]=(uint8_t)y;
            } else cover(w,(unsigned)x,(unsigned)y,(unsigned)y);
#endif
            ++stats->pixels_written;
        }
    }
#endif
}
