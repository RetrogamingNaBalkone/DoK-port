/* Full inset-high native ceiling strips at boundaries with lower insets. */
#ifdef DOK_RENDER_HW_GEOMETRY_PROFILE_PROTOTYPE
#define SIDES_NAME surface_sides_geometry_impl
#else
#define SIDES_NAME surface_sides
#endif
#ifdef DOK_RENDER_HW_HOT_SIDES_PROTOTYPE
static DOK_NOINLINE dok_renderer_status SIDES_NAME(dok_renderer_workspace *w,const dok_runtime *r,const dok_rom_assets *a,
#else
static DOK_COLD dok_renderer_status SIDES_NAME(dok_renderer_workspace *w,const dok_runtime *r,const dok_rom_assets *a,
#endif
    unsigned level,unsigned language,const dok_projection_camera *c,int x,int z,unsigned object) {
    int32_t inset;field(r->level,(uint16_t)object,0x54,&inset);
    if(inset<=0) return DOK_RENDER_OK;
#ifdef DOK_RENDER_SIDE_SETUP_PROTOTYPE
    unsigned side_mask=dok_static_cells[static_view][2][z*64+x];
#endif
#ifdef DOK_RENDER_HW_MERGE_RIMS_PROTOTYPE
    if(g_hw_tile_active && inset<=10) {
        /* BG2 already places inset0/10 at one base height. Preserve the
         * original image-consideration lease before omitting its tiny rim. */
        if(side_mask)return surface_image(w,r,a,level,language,object,2);
        return DOK_RENDER_OK;
    }
#endif
    for(unsigned side=0;side<4;++side) {
#ifdef DOK_RENDER_SIDE_SETUP_PROTOTYPE
        if(!(side_mask&(1u<<side)))continue;
#else
        const int nx[4]={0,-1,0,1},nz[4]={-1,0,1,0};uint16_t neighbor;int32_t other;
        if(!object_at(r,DOK_WORLD_FCMAP,x+nx[side],z+nz[side],&neighbor) || !field(r->level,neighbor,0x54,&other)) return DOK_RENDER_PROFILE;
        if(other>=inset) continue;
#endif
        dok_renderer_status status=surface_image(w,r,a,level,language,object,2);if(status) return status;
        const dok_image_view *im=&w->surface_images[object][2];
#ifdef DOK_RENDER_BACKFACE_SIDES_PROTOTYPE
        if(!(im->stored_flags&8)) {
            int eye_x=c->x+sar(c->sine*200,15),eye_z=c->z-sar(c->cosine*200,15),x0=x*480,z0=z*480;
            int inside=(c->x>=x0-4 && c->x<=x0+484 && c->z>=z0-4 && c->z<=z0+484) ||
                (eye_x>=x0-4 && eye_x<=x0+484 && eye_z>=z0-4 && eye_z<=z0+484);
            if(!inside && ((side==0 && eye_z>z0+4) || (side==1 && eye_x>x0+4) ||
                (side==2 && eye_z<z0+476) || (side==3 && eye_x<x0+476)))continue;
        }
#endif
        int32_t ax=x*480,az=z*480,bx=ax,bz=az;
        if(side==0) bx+=480;else if(side==1) az+=480;
        else if(side==2) {ax+=480;az+=480;bz+=480;}else {ax+=480;bx+=480;bz+=480;}
        dok_camera_point pa,pb;dok_screen_point at,bt,ab,bb;int32_t u0=0,u1=im->width-1;
#ifdef DOK_RENDER_GEOMETRY_CACHE_PROTOTYPE
        const geometry_vertex *va=geometry_transform(c,ax,-960,az,&pa),*vb=geometry_transform(c,bx,-960,bz,&pb);
        if(va && va==vb) va=0;
#else
        dok_projection_transform(c,ax,-960,az,&pa);dok_projection_transform(c,bx,-960,bz,&pb);
#endif
        if(pa.depth<=0 && pb.depth<=0) continue;
        if(pa.depth<0) {if(!clip_endpoint(&pa,&pb,&u0,u1)) return DOK_RENDER_PROJECTION;
#ifdef DOK_RENDER_GEOMETRY_CACHE_PROTOTYPE
            va=0;
#endif
        }
        else if(pb.depth<0) {if(!clip_endpoint(&pb,&pa,&u1,u0)) return DOK_RENDER_PROJECTION;
#ifdef DOK_RENDER_GEOMETRY_CACHE_PROTOTYPE
            vb=0;
#endif
        }
#ifdef DOK_RENDER_GEOMETRY_CACHE_PROTOTYPE
        if(!geometry_project(c,&pa,va,&at) || !geometry_project(c,&pb,vb,&bt)) return DOK_RENDER_PROJECTION;
#else
        if(!dok_projection_project(c,&pa,&at) || !dok_projection_project(c,&pb,&bt)) return DOK_RENDER_PROJECTION;
#endif
        pa.vertical+=inset;pb.vertical+=inset;
#if defined(DOK_RENDER_GEOMETRY_CACHE_PROTOTYPE) || defined(DOK_RENDER_SIDE_SETUP_PROTOTYPE)
        ab.y=add(sar(mul(pa.vertical,at.factor),15),c->center_y);
        bb.y=add(sar(mul(pb.vertical,bt.factor),15),c->center_y);
#else
        if(!dok_projection_project(c,&pa,&ab) || !dok_projection_project(c,&pb,&bb)) return DOK_RENDER_PROJECTION;
#endif
        int x0=scale_x(at.x),x1=scale_x(bt.x),t0=scale_y(at.y),t1=scale_y(bt.y),b0=scale_y(ab.y),b1=scale_y(bb.y);
        if(x0==x1 || (x0<0 && x1<0) || (x0>239 && x1>239) || (b0<0 && b1<0) || (t0>143 && t1>143)) continue;
        if(w->surface_count>=DOK_RENDER_SURFACE_CAPACITY) return DOK_RENDER_CAPACITY;
        dok_renderer_surface f;f.object=(uint16_t)object;f.plane=2;f.cell_x=(uint16_t)x;f.cell_z=(uint16_t)z;
        /* For plane2 v0 stores the native inclusive V end, with V start=0. */
        f.key=(pa.depth+pb.depth)/2;f.u0=(int16_t)u0;f.u1=(int16_t)u1;f.v0=word(im->height*inset/960);f.reserved=0;
        f.x[0]=f.x[3]=x0;f.x[1]=f.x[2]=x1;f.y[0]=t0;f.y[1]=t1;f.y[2]=b1;f.y[3]=b0;
        f.depth[0]=f.depth[3]=pa.depth;f.depth[1]=f.depth[2]=pb.depth;
        unsigned i=w->surface_count,j=i;
        while(j && w->surfaces[w->surface_order[j-1]].key<f.key) {w->surface_order[j]=w->surface_order[j-1];--j;}
        w->surfaces[i]=f;w->surface_order[j]=(uint16_t)i;++w->surface_count;
    }
    return DOK_RENDER_OK;
}
#ifdef DOK_RENDER_HW_GEOMETRY_PROFILE_PROTOTYPE
static DOK_COLD dok_renderer_status surface_sides(dok_renderer_workspace *w,const dok_runtime *r,const dok_rom_assets *a,
    unsigned level,unsigned language,const dok_projection_camera *c,int x,int z,unsigned object) {
    uint32_t stamp=render_ticks();dok_renderer_status result=surface_sides_geometry_impl(w,r,a,level,language,c,x,z,object);
    g_geometry_work[0]+=render_ticks()-stamp;++g_geometry_work[1];return result;
}
#endif
