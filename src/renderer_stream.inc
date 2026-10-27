/* Near-to-far segment BSP renderer. Static faces are projected and consumed
 * immediately; dynamic faces are split into screen-X fragments at BSP planes.
 * Existing texture samplers and affine ceiling mapping are intentionally reused
 * for an architectural comparison, without kernel tuning or a new light model. */
static DOK_COLD void stream_begin(dok_renderer_workspace *w,const dok_runtime *r,const dok_projection_camera *c,unsigned view) {
    stream_active=stream_emitting=stream_vertical_clip=0;stream_clip_first=0;stream_clip_last=239;
    for(unsigned k=0;k<6;++k)g_stream_work[k]=0;
    for(unsigned k=0;k<6;++k)g_stream_ticks[k]=0;
    uint32_t stamp=g_render_work[0]?render_ticks():0;
    if(!g_hw_tile_active)return;
    extern unsigned dok_gba_geometry_equal(const void *,const void *,unsigned);
    if(!dok_gba_geometry_equal(r->maps+4096,r->level->maps+4096,8192)) {++g_stream_work[6];return;}
    for(unsigned id=0;id<r->object_count;++id)if(r->level->definitions[id].class_id==2 &&
        ((r->objects[id].flags&0x800) || r->objects[id].visual_resource)) {++g_stream_work[6];return;}
    stream_camera=c;stream_runtime=r;stream_active=1;g_stream_work[0]=1;
    /* covered[] is a byte array at offset2 modulo4 in the public workspace.
     * ARM word loads rotate unaligned addresses; align this overlay explicitly. */
    uint8_t *scratch=(uint8_t *)(((uintptr_t)w->covered+3)&~(uintptr_t)3);
    stream_xy=(int32_t (*)[4])(void *)scratch;
    stream_heads=(uint16_t *)(void *)(scratch+4096);
    stream_present=scratch+8192;
    stream_fragments=(stream_fragment *)(void *)((uint8_t *)w->surfaces+1024);
    stream_fragment_count=0;
    unsigned n=dok_stream_views[view].node_count+dok_stream_views[view].leaf_count;
    for(unsigned k=0;k<n;++k) {stream_heads[k]=UINT16_MAX;stream_present[k]=0;}
    if(g_render_work[0])g_stream_ticks[0]+=render_ticks()-stamp;
}
static DOK_COLD void stream_record(unsigned index,int ax,int az,int bx,int bz,int da,int db) {
    if(da<0 && db>da) {ax+=(bx-ax)*(-da)/(db-da);az+=(bz-az)*(-da)/(db-da);}
    else if(db<0 && da>db) {bx+=(ax-bx)*(-db)/(da-db);bz+=(az-bz)*(-db)/(da-db);}
    stream_xy[index][0]=ax;stream_xy[index][1]=az;stream_xy[index][2]=bx;stream_xy[index][3]=bz;
}
static DOK_COLD void stream_emit_face(dok_renderer_workspace *w,const dok_renderer_face *f,dok_renderer_stats *s) {
    raster(w,stream_runtime,f,w->lights,s);
}
static DOK_COLD int stream_link(unsigned bucket,unsigned face,unsigned first,unsigned last) {
    if(first>last || first>239 || last<first)return 1;
    if(stream_fragment_count==4096) {++g_stream_work[7];return 0;}
    stream_fragment *f=&stream_fragments[stream_fragment_count];
    *f=(stream_fragment){(uint16_t)face,stream_heads[bucket],(uint8_t)first,(uint8_t)last,0};
    stream_heads[bucket]=(uint16_t)stream_fragment_count++;return 1;
}
static DOK_COLD int stream_place(const stream_view *v,int node,unsigned face,int ax,int az,int bx,int bz,unsigned first,unsigned last) {
    unsigned bucket=node<0?v->node_count+(unsigned)(-node-1):(unsigned)node;
    stream_present[bucket]=1;
    if(node<0)return stream_link(bucket,face,first,last);
    const stream_node *n=&v->nodes[node];
    int a=n->axis?az:ax,b=n->axis?bz:bx;
    if(a==n->fixed && b==n->fixed)return stream_link(bucket,face,first,last);
    if(a<=n->fixed && b<=n->fixed)return stream_place(v,n->child[0],face,ax,az,bx,bz,first,last);
    if(a>=n->fixed && b>=n->fixed)return stream_place(v,n->child[1],face,ax,az,bx,bz,first,last);
    int x=n->axis?ax+(bx-ax)*((int)n->fixed-a)/(b-a):(int)n->fixed;
    int z=n->axis?(int)n->fixed:az+(bz-az)*((int)n->fixed-a)/(b-a);
    dok_camera_point p;dok_screen_point screen;
    dok_projection_transform(stream_camera,x,0,z,&p);
    if(p.depth<0 || !dok_projection_project(stream_camera,&p,&screen)) {
        /* The recorded segment is near-clipped; retain both halves rather
         * than dropping geometry if scalar rounding crosses the near plane. */
        return stream_place(v,n->child[a>n->fixed],face,ax,az,x,z,first,last) &&
               stream_place(v,n->child[b>n->fixed],face,x,z,bx,bz,first,last);
    }
    int cut=scale_x(screen.x),left_first=(int)first,left_last=(int)last,right_first=(int)first,right_last=(int)last;
    /* Screen direction can reverse for center panels. The original face's
     * endpoints and UV interpolation remain unchanged; only its clip changes. */
    dok_camera_point endpoint;dok_screen_point es;
    dok_projection_transform(stream_camera,ax,0,az,&endpoint);
    int increasing=endpoint.depth>=0 && dok_projection_project(stream_camera,&endpoint,&es)?scale_x(es.x)<=cut:1;
    if(increasing) {if(left_last>cut)left_last=cut;if(right_first<cut+1)right_first=cut+1;}
    else {if(left_first<cut)left_first=cut;if(right_last>cut-1)right_last=cut-1;}
    int ok=1;
    if(left_first<=left_last && left_first<240 && left_last>=0)
        ok=stream_place(v,n->child[a>n->fixed],face,ax,az,x,z,(unsigned)clamp(left_first,0,239),(unsigned)clamp(left_last,0,239));
    if(ok && right_first<=right_last && right_first<240 && right_last>=0)
        ok=stream_place(v,n->child[b>n->fixed],face,x,z,bx,bz,(unsigned)clamp(right_first,0,239),(unsigned)clamp(right_last,0,239));
    return ok;
}
static DOK_COLD int stream_box_hidden(const dok_projection_camera *c,const uint16_t bounds[4]) {
    extern const uint32_t dok_plane_reciprocal[16385];
    dok_camera_point p;dok_projection_transform(c,bounds[0],0,bounds[1],&p);
    int wx=bounds[2]-bounds[0],wz=bounds[3]-bounds[1];
    int ax=sar(wx*c->cosine,15),az=sar(wz*c->sine,15),dx=-sar(wx*c->sine,15),dz=sar(wz*c->cosine,15);
    int lmin=p.lateral+(ax<0?ax:0)+(az<0?az:0)-3,lmax=p.lateral+(ax>0?ax:0)+(az>0?az:0)+3;
    int near=p.depth+(dx<0?dx:0)+(dz<0?dz:0)-3,far=p.depth+(dx>0?dx:0)+(dz>0?dz:0)+3;
    if(far<=0)return 1;
    if(near<=0 || far+200>16384)return 0;
    int left=119+(int)(((int64_t)lmin*150*dok_plane_reciprocal[(lmin<0?near:far)+200])>>31)-4;
    int right=119+(int)(((int64_t)lmax*150*dok_plane_reciprocal[(lmax>0?near:far)+200])>>31)+4;
    int a=-960-c->y,b=-c->y;
    int top=scale_y(79+(int)(((int64_t)a*200*dok_plane_reciprocal[(a<0?near:far)+200])>>31))-4;
    int bottom=scale_y(79+(int)(((int64_t)b*200*dok_plane_reciprocal[(b>0?near:far)+200])>>31))+4;
    if(right<0 || left>239 || bottom<0 || top>=128)return 1;
    left=(clamp(left,0,239)+1)&~1;right=clamp(right,0,239);top=clamp(top,0,127);bottom=clamp(bottom,0,127);
    for(int x=left;x<=right;x+=2)if(!coverage_contains((unsigned)x,(unsigned)top,(unsigned)bottom))return 0;
    return 1;
}
static DOK_COLD void stream_dynamic(dok_renderer_workspace *w,const dok_runtime *r,unsigned bucket,dok_renderer_stats *s,int ceiling) {
    uint32_t stamp=g_render_work[0]?render_ticks():0;
    stream_vertical_clip=1;stream_ceiling=ceiling;
    for(unsigned i=stream_heads[bucket];i!=UINT16_MAX;i=stream_fragments[i].next) {
        const stream_fragment *part=&stream_fragments[i];
        stream_clip_first=part->first;stream_clip_last=part->last;
        raster(w,r,&w->faces[part->face],w->lights,s);
    }
    stream_vertical_clip=0;stream_clip_first=0;stream_clip_last=239;
    if(g_render_work[0])g_stream_ticks[3]+=render_ticks()-stamp;
}
static DOK_COLD dok_renderer_status stream_atom_draw(dok_renderer_workspace *w,const dok_runtime *r,const dok_rom_assets *a,
    unsigned level,unsigned language,const dok_projection_camera *c,dok_renderer_stats *s,const stream_atom *p) {
    int x=p->cell%64,z=p->cell/64;
    if(x<g_hw_bg_envelope[0] || x>g_hw_bg_envelope[1] || z<g_hw_bg_envelope[2] || z>g_hw_bg_envelope[3])return DOK_RENDER_OK;
    if(p->kind && p->height<=10)return DOK_RENDER_OK;
    int ex=c->x+sar(c->sine*200,15),ez=c->z-sar(c->cosine*200,15),x0=x*480,z0=z*480;
    int inside=(c->x>=x0-4 && c->x<=x0+484 && c->z>=z0-4 && c->z<=z0+484) ||
        (ex>=x0-4 && ex<=x0+484 && ez>=z0-4 && ez<=z0+484);
    if(!inside && ((p->side==0 && ez>z0+4) || (p->side==1 && ex>x0+4) ||
                  (p->side==2 && ez<z0+476) || (p->side==3 && ex<x0+476)))return DOK_RENDER_OK;
    dok_renderer_status status;
    dok_renderer_image saved=w->images[p->object];
    if(p->kind) {
        status=surface_image(w,r,a,level,language,p->object,2);if(status)return status;
        const dok_image_view *im=&w->surface_images[p->object][2];
        extern const uint16_t dok_side_resources[6][256];
        extern const uint8_t dok_prelit_bank[];
        unsigned rid=dok_side_resources[static_view][p->object],off=prelit_view[rid*16];
        w->images[p->object]=(dok_renderer_image){im->pixels,dok_prelit_bank+off,0,im->width,im->height,im->stored_flags,(uint16_t)rid};
    } else {status=image(w,r,a,level,language,p->object);if(status)return status;}
    ++g_stream_work[3];
    status=plane(w,c,s,p->object,p->ax,p->az,p->bx,p->bz,p->y,p->height,p->v0,0);
    if(p->kind)w->images[p->object]=saved;
    return status;
}
static DOK_COLD dok_renderer_status stream_visit(dok_renderer_workspace *w,const dok_runtime *r,const dok_rom_assets *a,
    unsigned level,unsigned language,const dok_projection_camera *c,dok_renderer_stats *s,const stream_view *v,int node) {
    unsigned bucket=node<0?v->node_count+(unsigned)(-node-1):(unsigned)node;
    const uint16_t *bounds=node<0?v->leaves[-node-1].bounds:v->nodes[node].bounds;
    ++g_stream_work[1];
    if(!stream_present[bucket]) {
        int outside=bounds[0]>(g_hw_bg_envelope[1]+1)*480 || bounds[2]<g_hw_bg_envelope[0]*480 ||
            bounds[1]>(g_hw_bg_envelope[3]+1)*480 || bounds[3]<g_hw_bg_envelope[2]*480;
        uint32_t stamp=g_render_work[0]?render_ticks():0;
        int hidden=outside || stream_box_hidden(c,bounds);
        if(g_render_work[0])g_stream_ticks[2]+=render_ticks()-stamp;
        if(hidden) {
            ++g_stream_work[2];return DOK_RENDER_OK;
        }
    }
    dok_renderer_status status;
    if(node<0) {
        const stream_leaf *leaf=&v->leaves[-node-1];++g_stream_work[4];
        stream_dynamic(w,r,bucket,s,leaf->ceiling);
        stream_emitting=0;
        uint32_t stamp=g_render_work[0]?render_ticks():0;
        for(unsigned k=0;k<leaf->count;++k) {
            unsigned cell=v->cells[leaf->first+k];int x=(int)(cell%64),z=(int)(cell/64);
            if(x<g_hw_bg_envelope[0] || x>g_hw_bg_envelope[1] || z<g_hw_bg_envelope[2] || z>g_hw_bg_envelope[3])continue;
            unsigned fc=dok_static_cells[static_view][1][cell];w->surface_count=0;
            status=surfaces_build(w,r,a,level,language,c,x,z,fc);if(status)return status;
            for(unsigned j=0;j<w->surface_count;++j)surface_raster(w,r,&w->surfaces[j],s);
        }
        if(g_render_work[0])g_stream_ticks[5]+=render_ticks()-stamp;
        w->surface_count=0;stream_emitting=1;return DOK_RENDER_OK;
    }
    const stream_node *n=&v->nodes[node];
    int eye=n->axis?c->z-sar(c->cosine*200,15):c->x+sar(c->sine*200,15);
    unsigned side=eye>n->fixed;
    status=stream_visit(w,r,a,level,language,c,s,v,n->child[side]);if(status)return status;
    stream_dynamic(w,r,bucket,s,-951);
    uint32_t stamp=g_render_work[0]?render_ticks():0;
    for(unsigned k=0;k<n->count;++k) {
        status=stream_atom_draw(w,r,a,level,language,c,s,&v->atoms[v->ids[n->first+k]]);if(status)return status;
    }
    if(g_render_work[0])g_stream_ticks[4]+=render_ticks()-stamp;
    return stream_visit(w,r,a,level,language,c,s,v,n->child[side^1]);
}
static DOK_COLD dok_renderer_status stream_draw(dok_renderer_workspace *w,const dok_runtime *r,const dok_rom_assets *a,
    unsigned level,unsigned language,const dok_projection_camera *c,dok_renderer_stats *s) {
    const stream_view *v=&dok_stream_views[static_view];
    uint32_t stamp=g_render_work[0]?render_ticks():0;
    for(unsigned k=0;k<w->face_count;++k) {
        unsigned i=face_order[k];const dok_renderer_face *f=&w->faces[i];
        unsigned first=(unsigned)clamp(f->x0<f->x1?f->x0:f->x1,0,239),last=(unsigned)clamp(f->x0>f->x1?f->x0:f->x1,0,239);
        if(!stream_place(v,0,i,stream_xy[i][0],stream_xy[i][1],stream_xy[i][2],stream_xy[i][3],first,last))return DOK_RENDER_CAPACITY;
    }
    if(g_render_work[0])g_stream_ticks[1]+=render_ticks()-stamp;
    g_stream_work[5]=stream_fragment_count;stream_emitting=1;
    dok_renderer_status status=stream_visit(w,r,a,level,language,c,s,v,0);
    stream_emitting=0;stream_vertical_clip=0;stream_clip_first=0;stream_clip_last=239;return status;
}
