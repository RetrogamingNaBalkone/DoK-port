/* G10: native horizontal vertex construction and bounded affine scanlines.
 * Included after scalar helpers; no original far pointers cross this boundary. */
unsigned dok_renderer_light_row(int32_t depth,int16_t near) {
    /* Native SUB word/JGE observes signed overflow, then signed IDIV120.
     * Depth is the SAR16 integer before truncation to AX. near=1920. */
    int16_t d=word(depth);int n=d<near?0:word((int)d-near);
    unsigned row=(uint16_t)(n/120);
    return row>15?15:row;
}
static unsigned light_index(int32_t depth) {
    unsigned row=dok_renderer_light_row(depth,1920);
#ifdef DOK_RENDER_HW_LIGHT4_PROTOTYPE
    return row&12u;
#else
    return row;
#endif
}
#if defined(DOK_RENDERER_PROBE) && !defined(DOK_RENDER_WORLD_PLANES_PROTOTYPE)
typedef struct {uint32_t tag;int32_t depth,factor,x;} surface_vertex;
static surface_vertex surface_vertices[256];
static uint32_t surface_epoch;
static surface_vertex *surface_vertex_at(const dok_projection_camera *c,unsigned x,unsigned z) {
    unsigned key=z*65+x;
    surface_vertex *v=&surface_vertices[key&255];
    uint32_t tag=(surface_epoch<<13)|key;
    if(v->tag!=tag) {
        dok_camera_point p;dok_screen_point s;
        dok_projection_transform(c,(int)x*480,0,(int)z*480,&p);
        if(p.depth<0) p.depth=sar(p.depth,5);
        int valid=dok_projection_project(c,&p,&s);
        v->depth=p.depth;v->factor=valid?s.factor:INT_MIN;v->x=valid?scale_x(s.x):0;v->tag=tag;
    }
    return v;
}
#endif
static DOK_COLD dok_renderer_status surface_image(dok_renderer_workspace *w,const dok_runtime *r,
    const dok_rom_assets *a,unsigned level,unsigned language,unsigned object,unsigned plane) {
    uint16_t id;dok_rom_resource res;unsigned slot=plane==2?0x5e:(plane?0x56:0x5a);
    if(w->surface_known[object][plane]) return DOK_RENDER_OK;
#ifdef DOK_RENDER_STATIC_SETUP_PROTOTYPE
    if(surface_cache_view[object][plane]==static_view+1) {w->surface_known[object][plane]=1;return DOK_RENDER_OK;}
#endif
    const dok_runtime_definition *d=&r->level->definitions[object];
    /* Class12/13 override the floor getter with their configured Sprite. */
    if(!plane && (d->class_id==12 || d->class_id==13)) slot=DOK_ROM_SLOT_SPRITE;
    if(dok_rom_binding(&id,a,level,language,(uint16_t)object,(uint16_t)slot)!=DOK_ROM_OK ||
       dok_rom_resource_at(&res,a,id,language)!=DOK_ROM_OK ||
       dok_image_open(res.data,res.size,&w->surface_images[object][plane])!=DOK_ASSET_OK) return DOK_RENDER_RESOURCE;
    dok_image_view *im=&w->surface_images[object][plane];
    if(!im->width || !im->height || im->width>256 || im->height>512) return DOK_RENDER_PROFILE;
#ifdef DOK_RENDER_STATIC_SETUP_PROTOTYPE
    surface_cache_view[object][plane]=(uint8_t)(static_view+1);
#endif
    w->surface_known[object][plane]=1;return DOK_RENDER_OK;
}
#ifndef DOK_RENDER_WORLD_PLANES_PROTOTYPE
#ifdef DOK_RENDER_HW_GEOMETRY_PROFILE_PROTOTYPE
#define SURFACES_NAME surfaces_build_geometry_impl
#else
#define SURFACES_NAME surfaces_build
#endif
#ifdef DOK_RENDER_COLUMN_PIPELINE_PROTOTYPE
static DOK_COLD dok_renderer_status SURFACES_NAME(dok_renderer_workspace *w,const dok_runtime *r,const dok_rom_assets *a,
#else
static DOK_NOINLINE dok_renderer_status SURFACES_NAME(dok_renderer_workspace *w,const dok_runtime *r,const dok_rom_assets *a,
#endif
    unsigned level,unsigned language,const dok_projection_camera *c,int x,int z,unsigned object) {
    int32_t inset;if(!field(r->level,(uint16_t)object,0x54,&inset)) return DOK_RENDER_PROFILE;
#ifdef DOK_RENDER_HW_BG_LEASE_CACHE_PROTOTYPE
    /* GPU planes need no per-cell vertices after both original image
     * consideration leases have been published for this object this draw. */
    if(g_hw_tile_active && inset<=10 && w->surface_known[object][0] && w->surface_known[object][1])return DOK_RENDER_OK;
#endif
    dok_camera_point p[4];const int cx[4]={0,1,1,0},cz[4]={1,1,0,0};
#ifdef DOK_RENDERER_PROBE
    const surface_vertex *vertices[4];
#endif
    int visible=0;int32_t key;
    for(unsigned k=0;k<4;++k) {
#ifdef DOK_RENDERER_PROBE
        const surface_vertex *v=surface_vertex_at(c,(unsigned)(x+cx[k]),(unsigned)(z+cz[k]));
        if(!v) return DOK_RENDER_PROJECTION;
        vertices[k]=v;p[k].depth=v->depth;
        if(v->depth>0) visible=1;
#else
        dok_projection_transform(c,(x+cx[k])*480,0,(z+cz[k])*480,&p[k]);
        if(p[k].depth>0) visible=1;
        /* Original horizontal-only treatment of negative rotation numerator:
         * SAR20 rather than clipping a polygon at depth0. */
        if(p[k].depth<0) p[k].depth=sar(p[k].depth,5);
#endif
    }
    if(!visible) return DOK_RENDER_OK;
#ifdef DOK_RENDERER_PROBE
    int left=INT_MAX,right=INT_MIN;
    for(unsigned k=0;k<4;++k) {
        if(vertices[k]->factor==INT_MIN) return DOK_RENDER_PROJECTION;
        if(vertices[k]->x<left) left=vertices[k]->x;
        if(vertices[k]->x>right) right=vertices[k]->x;
    }
    if(right<0 || left>239) return DOK_RENDER_OK;
#endif
    key=sar(add(p[0].depth,p[2].depth),1);if(key<0) key=0;
    for(unsigned plane=0;plane<(inset<0?1u:2u);++plane) {
#ifdef DOK_RENDER_HW_BG_LEASE_CACHE_PROTOTYPE
        if(g_hw_tile_active && !plane && w->surface_known[object][0])continue;
#endif
        dok_renderer_surface f;int ymin=INT_MAX,ymax=INT_MIN,xmin=INT_MAX,xmax=INT_MIN;
        int32_t wy=plane?word(inset-961):word(-r->level->definitions[object].height_parameter);
        f.object=(uint16_t)object;f.plane=(uint16_t)plane;f.key=key;
        f.cell_x=(uint16_t)x;f.cell_z=(uint16_t)z;f.u0=f.u1=f.v0=f.reserved=0;
        for(unsigned k=0;k<4;++k) {
            unsigned n=plane?3-k:k;dok_screen_point s;
#ifdef DOK_RENDERER_PROBE
            const surface_vertex *v=vertices[n];
            s.y=add(sar(mul(sub(wy,c->y),v->factor),15),c->center_y);
            f.x[k]=v->x;
#else
            p[n].vertical=sub(wy,c->y);
            if(!dok_projection_project(c,&p[n],&s)) return DOK_RENDER_PROJECTION;
            f.x[k]=scale_x(s.x);
#endif
            f.y[k]=scale_y(s.y);f.depth[k]=p[n].depth;
            if(f.x[k]<-8192 || f.x[k]>8192 || f.y[k]<-8192 || f.y[k]>8192) return DOK_RENDER_PROFILE;
            if(f.x[k]<xmin) xmin=f.x[k];if(f.x[k]>xmax) xmax=f.x[k];
            if(f.y[k]<ymin) ymin=f.y[k];if(f.y[k]>ymax) ymax=f.y[k];
        }
        if(xmax<0 || xmin>239 || ymax<0 || ymin>143 || ymax==ymin) continue;
        if(w->surface_count>=DOK_RENDER_SURFACE_CAPACITY) return DOK_RENDER_CAPACITY;
        dok_renderer_status status=surface_image(w,r,a,level,language,object,plane);if(status) return status;
#ifdef DOK_RENDER_HW_BG_PROTOTYPE
        /* Preserve original consideration/animation leases before omitting
         * the GPU plane. Lowered ceilings and vertical strips stay on CPU. */
        if(g_hw_tile_active && (!plane || inset<=10))continue;
#endif
        unsigned i=w->surface_count,j=i;
        while(j && w->surfaces[w->surface_order[j-1]].key<key) {
            w->surface_order[j]=w->surface_order[j-1];--j;
        }
        w->surfaces[i]=f;w->surface_order[j]=(uint16_t)i;++w->surface_count;
    }
    return DOK_RENDER_OK;
}
#ifdef DOK_RENDER_HW_GEOMETRY_PROFILE_PROTOTYPE
static DOK_COLD dok_renderer_status surfaces_build(dok_renderer_workspace *w,const dok_runtime *r,const dok_rom_assets *a,
    unsigned level,unsigned language,const dok_projection_camera *c,int x,int z,unsigned object) {
    uint32_t stamp=render_ticks();dok_renderer_status result=surfaces_build_geometry_impl(w,r,a,level,language,c,x,z,object);
    g_geometry_work[2]+=render_ticks()-stamp;++g_geometry_work[3];return result;
}
#endif
#ifdef DOK_RENDER_HW_NEAR_FC_PROTOTYPE
static DOK_COLD dok_renderer_status surfaces_build_hardware(dok_renderer_workspace *w,const dok_runtime *r,const dok_rom_assets *a,
    unsigned level,unsigned language,const dok_projection_camera *c,int x,int z,unsigned object) {
    int inset=dok_static_insets[static_view][object];
    if(g_hw_tile_active && inset>=0 && inset<=10 && w->surface_known[object][0]) {
        if(w->surface_known[object][1])return DOK_RENDER_OK;
        if(c->y==-300) {
            int sa=c->sine<0?-c->sine:c->sine,ca=c->cosine<0?-c->cosine:c->cosine;
            int dx=x*480+240-c->x,dz=z*480+240-c->z;
            int far=sar(sub(mul(dz,c->cosine),mul(dx,c->sine)),15)+sar((sa+ca)*240,15)+2;
            /* Inset0/10 ceilings lie above the viewport for every corner
             * when raw far depth<=1200 at camera.y=-300. The floor lease is
             * already known, and the GPU path emits neither surface. */
            if(far<=1200)return DOK_RENDER_OK;
        }
    }
    return surfaces_build(w,r,a,level,language,c,x,z,object);
}
#endif
#endif
