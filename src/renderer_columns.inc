/* Entire foreground pipeline: DDA static boundaries, dynamic face columns,
 * nearest visible ownership per Y, then vertical output and shared ceiling rows.
 * Existing BG2 draws the base floor/ceiling. World-space ceiling UV is a visual
 * variant of the old per-cell affine mapper; heights and source art remain. */
static DOK_COLD dok_renderer_status column_begin(dok_renderer_workspace *w,const dok_runtime *r,const dok_rom_assets *a,
    unsigned level,unsigned language,const dok_projection_camera *c) {
    column_active=0;column_ceiling_object=UINT16_MAX;
    for(unsigned k=0;k<8;++k)g_column_work[k]=0;
    for(unsigned k=0;k<4;++k)g_column_ticks[k]=0;
    if(!g_hw_tile_active || c->y<=-560)return DOK_RENDER_OK;
    for(unsigned id=0;id<r->object_count;++id) {
        int inset=dok_static_insets[static_view][id];
        if(inset && (inset!=10 && inset!=400))return DOK_RENDER_OK;
        if(inset && r->level->definitions[id].height_parameter)return DOK_RENDER_OK;
        if(inset==400 && column_ceiling_object==UINT16_MAX)column_ceiling_object=id;
        if(r->level->definitions[id].class_id!=2)continue;
        if((r->objects[id].flags&0x800) || r->objects[id].visual_resource)return DOK_RENDER_OK;
        unsigned lease=w->image_known[id];
        dok_renderer_status status=image(w,r,a,level,language,(uint16_t)id);if(status)return status;
        w->image_known[id]=(uint8_t)lease;
        const dok_renderer_image *im=&w->images[id];int scale=r->level->definitions[id].width_parameter;
        if(im->width*scale!=480 || im->height*scale!=960 || (im->flags&8) || !im->columns)return DOK_RENDER_OK;
    }
    uint8_t *scratch=(uint8_t *)(((uintptr_t)w->covered+3)&~(uintptr_t)3);
    column_ceiling_mask=(uint32_t (*)[4])(void *)scratch;
    column_ceiling_depth=(uint16_t *)(void *)(scratch+1920);
    column_edges=(column_edge_state (*)[2])(void *)(scratch+2176);
    column_faces=(column_face *)(void *)w->surfaces;
    column_events=(column_event *)(void *)((uint8_t *)w->surfaces+256*sizeof(column_face));
    column_active=1;g_column_work[0]=1;return DOK_RENDER_OK;
}
static DOK_NOINLINE unsigned column_color(const column_event *e,unsigned y) {
    unsigned v=e->v+(y-e->first)*e->step;
    unsigned color=e->pixels[(v>>16)*e->stride];
    return e->lut?e->lut[color]:color;
}
static DOK_NOINLINE void column_offer(unsigned id,const column_event *e,uint16_t depth[128],uint16_t owner[128],uint8_t cached[128]) {
    for(unsigned y=e->first;y<=e->last;++y)if(e->depth<depth[y]) {
        if(e->masked) {
            ++g_column_work[4];unsigned color=column_color(e,y);if(!color)continue;
            cached[y]=(uint8_t)color;
        }
        depth[y]=e->depth;owner[y]=(uint16_t)id;
    }
}
static DOK_NOINLINE int column_add_event(column_event event,unsigned *count,uint16_t depth[128],uint16_t owner[128],uint8_t cached[128]) {
    if(*count==384) {++g_column_work[7];return 0;}
    column_events[*count]=event;
    column_offer(*count,&column_events[*count],depth,owner,cached);++*count;return 1;
}
static void column_edge_advance(column_edge_state *e) {
    e->quotient+=e->step;e->remainder+=e->carry;
    if(e->remainder>=e->divisor) {e->remainder-=e->divisor;++e->quotient;}
}
static DOK_NOINLINE int column_dynamic(dok_renderer_workspace *w,const dok_runtime *r,unsigned x,unsigned *count,
    uint16_t depth[128],uint16_t owner[128],uint8_t cached[128]) {
    extern const uint8_t dok_prelit_bank[];
    for(unsigned rank=w->face_count;rank>0;--rank) {
        unsigned k=face_order[rank-1];
        column_face *d=&column_faces[k];const dok_renderer_face *f=&d->f;const dok_renderer_image *im=d->image;
        if((int)x<f->x0 || (int)x>f->x1)continue;
        int i=(int)x-f->x0;
        column_edge_state *upper=&column_edges[k][0],*lower=&column_edges[k][1];
        int top=f->top0+upper->sign*(int)upper->quotient,bottom=f->bottom0+lower->sign*(int)lower->quotient;
        column_edge_advance(upper);column_edge_advance(upper);
        column_edge_advance(lower);column_edge_advance(lower);
        int u=d->u;
        if(d->rectangle) {
            d->u+=d->u_step;d->remainder+=d->u_carry;
            if(d->remainder<0) {--d->u;d->remainder+=d->span;}
            else if(d->remainder>=d->span) {++d->u;d->remainder-=d->span;}
        }
        if(bottom<top || bottom<0 || top>=128)continue;
        int first=clamp(top,0,127),last=clamp(bottom,0,127);
        int denominator=d->q0*(d->span-i)+d->q1*i;
        if(!d->rectangle)u=(f->u0*d->q0*(d->span-i)+f->u1*d->q1*i)/denominator;
        u=clamp(u,0,im->width-1);
        int crop=d->crop_y;if(u<d->crop_x)continue;u-=d->crop_x;
        unsigned start=(unsigned)clamp(f->v0,0,im->height-1);
        unsigned step;
        if(d->rectangle) {first=d->range.rect.first;last=d->range.rect.last;step=d->step;}
        else if(d->range.height==bottom-top)step=d->step;
        else {step=bottom>top?((im->height-1-start)<<16)/(unsigned)(bottom-top):0;d->step=step;d->range.height=(int16_t)(bottom-top);}
        unsigned v=(start<<16)+(unsigned)(first-top)*step;
        if(crop) {
            unsigned threshold=(unsigned)crop<<16;
            if(v<threshold) {
                if(!step)continue;
                unsigned skip=(threshold-v+step-1)/step;first+=(int)skip;v+=skip*step;
                if(first>last)continue;
            }
            v-=threshold;
        }
        int z=f->depth0==f->depth1?f->depth0:
            (f->depth0*d->q0*(d->span-i)+f->depth1*d->q1*i)/denominator;
        unsigned row=d->rectangle?d->row:light_index(z),prelit=im->resource_id?prelit_view[im->resource_id*16+row]:0;
        column_event e={0,0,v,step,(uint16_t)clamp(z+1,1,65534),1,(uint8_t)first,(uint8_t)last,(uint8_t)((im->flags&8)!=0),0};
        if(im->columns && prelit)e.pixels=dok_prelit_bank+prelit+(unsigned)u*im->height;
        else if(im->columns) {e.pixels=im->columns+(unsigned)u*im->height;e.lut=w->lights+row*256;}
        else {e.pixels=im->pixels+u;e.stride=im->width;e.lut=w->lights+row*256;}
        if(!column_add_event(e,count,depth,owner,cached))return 0;
    }
    (void)r;return 1;
}
static DOK_NOINLINE void column_ceiling(unsigned entry,unsigned exit,uint16_t depth[128],uint16_t owner[128],const dok_projection_camera *c) {
    int top=scale_y(sar(mul(-561-c->y,6553600/((int)(entry/2)+200)),15)+79);
    int bottom=scale_y(sar(mul(-561-c->y,6553600/((int)(exit/2)+200)),15)+79);
    if(bottom<0 || top>=128 || bottom<top)return;
    top=clamp(top-1,0,127);bottom=clamp(bottom+1,0,127);
    for(int y=top;y<=bottom;++y) {
        unsigned z=column_ceiling_depth[y];
        if(z==65535 || z*2<entry || z*2>exit)continue;
        if(z+1<depth[y]) {depth[y]=(uint16_t)(z+1);owner[y]=65534;}
    }
}
static DOK_NOINLINE int column_boundary(dok_renderer_workspace *w,const dok_runtime *r,unsigned object,int height,int bottom,
    int v0,unsigned u,unsigned z,unsigned *count,uint16_t depth[128],uint16_t owner[128],uint8_t cached[128],const dok_projection_camera *c,unsigned rim) {
    extern const uint8_t dok_prelit_bank[];
    extern const uint16_t dok_side_resources[6][256];
    unsigned rid;const dok_renderer_image *im=&w->images[object];const dok_image_view *side=&w->surface_images[object][2];
    unsigned width=rim?side->width:im->width,rows=rim?side->height:im->height,flags=rim?side->stored_flags:im->flags;
    rid=rim?dok_side_resources[static_view][object]:im->resource_id;
    int factor=6553600/((int)z+200),top=scale_y(sar(mul(bottom-height-c->y,factor),15)+79);
    bottom=scale_y(sar(mul(bottom-c->y,factor),15)+79);
    if(top>=128 || bottom<0 || bottom<top)return 1;
    unsigned first=(unsigned)clamp(top,0,127),last=(unsigned)clamp(bottom,0,127);
    unsigned v=(unsigned)clamp(v0,0,(int)rows-1),step=bottom>top?((rows-1-v)<<16)/(unsigned)(bottom-top):0;
    v=(v<<16)+(first-top)*step;
    unsigned row=light_index((int)z),offset=prelit_view[rid*16+row];
    u=u*(width-1)/480;if(u>=width)u=width-1;
    column_event e={dok_prelit_bank+offset+u*rows,0,v,step,(uint16_t)clamp((int)z+1,1,65534),1,
                    (uint8_t)first,(uint8_t)last,(uint8_t)((flags&8)!=0),(uint8_t)(rim?2:1)};
    (void)r;return column_add_event(e,count,depth,owner,cached);
}
static DOK_NOINLINE int column_static(dok_renderer_workspace *w,const dok_runtime *r,const dok_projection_camera *c,unsigned x,
    unsigned *count,uint16_t depth[128],uint16_t owner[128],uint8_t cached[128]) {
    int lateral=(int)x*4/3-159;
    int32_t ox=add(c->x*65536,c->cosine*lateral*2),oz=add(c->z*65536,c->sine*lateral*2);
    int32_t dx=-c->sine+c->cosine*lateral/200,dz=c->cosine+c->sine*lateral/200;
    int cx=(ox>>16)/480,cz=(oz>>16)/480,sx=dx<0?-1:1,sz=dz<0?-1:1;
    unsigned ax=(unsigned)(dx<0?-dx:dx),az=(unsigned)(dz<0?-dz:dz);
    unsigned tx=ax?(unsigned)(sx>0?(cx+1)*480*65536-ox:ox-cx*480*65536)/ax:UINT_MAX;
    unsigned tz=az?(unsigned)(sz>0?(cz+1)*480*65536-oz:oz-cz*480*65536)/az:UINT_MAX;
    unsigned stepx=ax?31457280u/ax:UINT_MAX,stepz=az?31457280u/az:UINT_MAX,entry=0;
    for(unsigned n=0;n<40;++n) {
        if(cx<g_hw_bg_envelope[0] || cx>g_hw_bg_envelope[1] || cz<g_hw_bg_envelope[2] || cz>g_hw_bg_envelope[3])break;
        ++g_column_work[2];uint16_t object,fc;
        if(!object_at(r,DOK_WORLD_TMAP,cx,cz,&object) || !object_at(r,DOK_WORLD_FCMAP,cx,cz,&fc))return 0;
        unsigned cls=r->level->definitions[object].class_id;
        int inset=dok_static_insets[static_view][fc];
        unsigned hit=tx<tz?tx:tz,axis=tx<tz?0:1;
        if(cls!=2 && inset==400)column_ceiling(entry,hit,depth,owner,c);
        int nx=cx+(axis==0?sx:0),nz=cz+(axis==1?sz:0);uint16_t next,nfc;
        if(nx<0 || nx>63 || nz<0 || nz>63)break;
        if(nx<g_hw_bg_envelope[0] || nx>g_hw_bg_envelope[1] || nz<g_hw_bg_envelope[2] || nz>g_hw_bg_envelope[3])break;
        if(!object_at(r,DOK_WORLD_TMAP,nx,nz,&next) || !object_at(r,DOK_WORLD_FCMAP,nx,nz,&nfc))return 0;
        unsigned nextcls=r->level->definitions[next].class_id;
        int nextinset=dok_static_insets[static_view][nfc];
        int coordinate=axis?sar(add(ox,mul(dx,(int)hit)),16):sar(add(oz,mul(dz,(int)hit)),16);
        int phase=coordinate%480;if(phase<0)phase+=480;
        int flip=(axis && sz<0) || (!axis && sx>0);
        if(cls!=2 && nextcls==2) {
            const dok_renderer_image *im=&w->images[next];
            if(!column_boundary(w,r,next,961-inset,0,(int)im->height*inset/960,(unsigned)(flip?480-phase:phase),hit/2,count,depth,owner,cached,c,0))return 0;
            break;
        }
        if(cls==2 && nextcls!=2) {
            const dok_renderer_image *im=&w->images[object];
            if(!column_boundary(w,r,object,961-nextinset,0,(int)im->height*nextinset/960,(unsigned)(flip?phase:480-phase),hit/2,count,depth,owner,cached,c,0))return 0;
            break;
        }
        if(cls!=2 && nextcls!=2 && nextinset>inset && nextinset>10) {
            const dok_image_view *im=&w->surface_images[nfc][2];
            if(!column_boundary(w,r,nfc,nextinset,nextinset-960,(int)im->height*nextinset/960,
                (unsigned)(flip?480-phase:phase),hit/2,count,depth,owner,cached,c,1))return 0;
        }
        if(axis==0)tx+=stepx;else tz+=stepz;
        cx=nx;cz=nz;entry=hit;
    }
    return 1;
}
static DOK_NOINLINE void column_output(dok_renderer_workspace *w,unsigned x,unsigned count,
    const uint16_t owner[128],const uint8_t cached[128],dok_renderer_stats *s) {
    unsigned used=0;
    for(unsigned y=0;y<128;) {
        unsigned first=y,id=owner[y++];if(id==65535)continue;
        if(id==65534) {column_ceiling_mask[x>>1][first>>5]|=1u<<(first&31);continue;}
        if(id>=count)continue;
        while(y<128 && owner[y]==id)++y;
        unsigned n=y-first;
        const column_event *e=&column_events[id];
        extern unsigned dok_gba_tiled_column(uint8_t *,const uint8_t *,unsigned,unsigned,unsigned,unsigned,unsigned);
        if(e->masked || (!e->lut && e->stride==1)) {
            const uint8_t *source=e->masked?cached+first:e->pixels;
            unsigned v=e->masked?0:e->v+(first-e->first)*e->step,step=e->masked?65536:e->step;
            dok_gba_tiled_column(scene_pointer(w->frame,x,first),source,v,step,n,first&7,g_hw_tile_black);
        } else for(unsigned row=first;row<y;++row) {
            unsigned color=column_color(e,row);
            *scene_pointer(w->frame,x,row)=(uint8_t)(color?color:g_hw_tile_black);
        }
        COVER_MARK(x,first,y-1);used+=n;
    }
    if(used)COVER_COUNT(w,x)=128;
    s->pixels_written+=used;g_column_work[5]+=used;
}
static DOK_NOINLINE void column_ceiling_output(dok_renderer_workspace *w,const dok_projection_camera *c) {
    if(column_ceiling_object==UINT16_MAX)return;
    const dok_image_view *im=&w->surface_images[column_ceiling_object][1];
    for(unsigned y=0;y<128;++y) {
        unsigned depth=column_ceiling_depth[y];if(depth==65535)continue;
        unsigned word=y>>5,mask=1u<<(y&31),row=light_index((int)depth);const uint8_t *lut=w->lights+row*256;
        int distance=(int)depth+200;
        int32_t lateral=-distance*52101,step=distance*65536/75;
        int32_t wx=(c->x*65536)/480+((int32_t)(((int64_t)lateral*c->cosine)>>15))/480-(c->sine*(int)depth*2)/480;
        int32_t wz=(c->z*65536)/480+((int32_t)(((int64_t)lateral*c->sine)>>15))/480+(c->cosine*(int)depth*2)/480;
        int32_t dx=((int32_t)(((int64_t)step*c->cosine)>>15))/480,dz=((int32_t)(((int64_t)step*c->sine)>>15))/480;
        for(unsigned x=0;x<120;++x,wx+=dx,wz+=dz)if(column_ceiling_mask[x][word]&mask) {
            unsigned u=((unsigned)wx&65535)*(im->width-1)>>16,v=((unsigned)wz&65535)*(im->height-1)>>16;
            unsigned color=lut[im->pixels[v*im->width+u]];
            *scene_pointer(w->frame,x*2,y)=(uint8_t)(color?color:g_hw_tile_black);
            coverage_bits[x][word]|=mask;COVER_COUNT(w,x*2)=128;++g_column_work[6];
        }
    }
}
static DOK_COLD dok_renderer_status column_draw(dok_renderer_workspace *w,const dok_runtime *r,const dok_rom_assets *a,
    unsigned level,unsigned language,const dok_projection_camera *c,dok_renderer_stats *s) {
    uint32_t stamp=g_render_work[0]?render_ticks():0;
    for(unsigned id=0;id<r->object_count;++id)if(r->level->definitions[id].class_id==2) {
        unsigned lease=w->image_known[id];dok_renderer_status status=image(w,r,a,level,language,(uint16_t)id);if(status)return status;
        w->image_known[id]=(uint8_t)lease;
    }
    if(column_ceiling_object!=UINT16_MAX) {
        unsigned lease=w->surface_known[column_ceiling_object][1];
        dok_renderer_status status=surface_image(w,r,a,level,language,column_ceiling_object,1);if(status)return status;
        w->surface_known[column_ceiling_object][1]=(uint8_t)lease;
    }
    for(unsigned k=0;k<w->face_count;++k) {
        column_face *d=&column_faces[k];d->f=w->faces[k];dok_renderer_face *f=&d->f;
        if(f->x0>f->x1) {
            swap32(&f->x0,&f->x1);swap32(&f->top0,&f->top1);swap32(&f->bottom0,&f->bottom1);
            swap32(&f->depth0,&f->depth1);swap32(&f->u0,&f->u1);
        }
        d->span=f->x1-f->x0;d->q0=(6553600/(f->depth0+200))>>8;d->q1=(6553600/(f->depth1+200))>>8;
        if(d->q0<1)d->q0=1;if(d->q1<1)d->q1=1;d->image=face_image(w,f->image_index);
        unsigned first=(unsigned)(clamp(f->x0,0,239)+1)&~1u;
        edge_walk upper=edge_start(f->top1-f->top0,first-f->x0,(unsigned)d->span),lower=edge_start(f->bottom1-f->bottom0,first-f->x0,(unsigned)d->span);
        _Static_assert(sizeof(edge_walk)==sizeof(column_edge_state),"column edge walker ABI");
        column_edges[k][0]=(column_edge_state){upper.quotient,upper.remainder,upper.step,upper.carry,upper.divisor,upper.sign};
        column_edges[k][1]=(column_edge_state){lower.quotient,lower.remainder,lower.step,lower.carry,lower.divisor,lower.sign};
        d->rectangle=(uint8_t)(f->depth0==f->depth1 && f->top0==f->top1 && f->bottom0==f->bottom1);
        int numerator=f->u0*d->span+(f->u1-f->u0)*((int)first-f->x0);
        d->u=numerator/d->span;d->remainder=numerator%d->span;
        d->u_step=(f->u1-f->u0)*2/d->span;d->u_carry=(f->u1-f->u0)*2%d->span;
        d->crop_x=d->crop_y=0;d->row=(uint8_t)light_index(f->depth0);d->range.height=-1;
        if(f->image_index<r->object_count && r->level->definitions[f->image_index].class_id==7) {
            const dok_runtime_object *o=&r->objects[f->image_index];int divisor=o->state.door.extent;
            if(divisor<1)divisor=1;
            if(d->image->flags&0x40)d->crop_x=(int16_t)(d->image->width/divisor*(int16_t)o->frame);
            else if(d->image->flags&0x100)d->crop_y=(int16_t)(d->image->height/divisor*(int16_t)o->frame);
        }
        if(d->rectangle) {
            d->range.rect.first=(uint8_t)clamp(f->top0,0,127);d->range.rect.last=(uint8_t)clamp(f->bottom0,0,127);
            unsigned start=(unsigned)clamp(f->v0,0,d->image->height-1);
            d->step=f->bottom0>f->top0?((d->image->height-1-start)<<16)/(unsigned)(f->bottom0-f->top0):0;
        }
    }
    int far=0;
    for(unsigned corner=0;corner<4;++corner) {
        dok_camera_point p;
        int x=(corner&1?g_hw_bg_envelope[1]+1:g_hw_bg_envelope[0])*480;
        int z=(corner&2?g_hw_bg_envelope[3]+1:g_hw_bg_envelope[2])*480;
        dok_projection_transform(c,x,0,z,&p);if(p.depth+4>far)far=p.depth+4;
    }
    for(unsigned y=0;y<128;++y) {
        int dy=(int)y*5/4-79,z=dy<0?(-561-c->y)*200/dy-200:-1;
        column_ceiling_depth[y]=(uint16_t)(z>=0 && z<=far?z:65535);
    }
    zero(column_ceiling_mask,1920);
    if(g_render_work[0])g_column_ticks[0]+=render_ticks()-stamp;
    for(unsigned x=0;x<240;x+=2) {
        uint16_t depth[128],owner[128];uint8_t cached[128];unsigned count=0;
        for(unsigned y=0;y<128;++y)depth[y]=owner[y]=65535;
        stamp=g_render_work[0]?render_ticks():0;
        if(!column_static(w,r,c,x,&count,depth,owner,cached) || !column_dynamic(w,r,x,&count,depth,owner,cached))return DOK_RENDER_CAPACITY;
        if(g_column_probe[0]==x+1) {
            uint8_t *probe=(uint8_t *)column_ceiling_mask+16384;
            for(unsigned y=0;y<128;++y) {
                ((uint16_t *)(void *)probe)[y]=depth[y];
                ((uint16_t *)(void *)(probe+256))[y]=owner[y];probe[512+y]=cached[y];
            }
            const uint8_t *src=(const uint8_t *)(const void *)column_events;
            for(unsigned i=0;i<count*sizeof(column_event);++i)probe[640+i]=src[i];
            g_column_probe[2]=count;g_column_probe[3]=x;++g_column_probe[1];
        }
        if(g_render_work[0])g_column_ticks[1]+=render_ticks()-stamp;
        ++g_column_work[1];g_column_work[3]+=count;
        stamp=g_render_work[0]?render_ticks():0;column_output(w,x,count,owner,cached,s);
        if(g_render_work[0])g_column_ticks[2]+=render_ticks()-stamp;
    }
    stamp=g_render_work[0]?render_ticks():0;column_ceiling_output(w,c);s->pixels_written+=g_column_work[6];
    if(g_render_work[0])g_column_ticks[3]+=render_ticks()-stamp;
    w->surface_count=0;return DOK_RENDER_OK;
}
