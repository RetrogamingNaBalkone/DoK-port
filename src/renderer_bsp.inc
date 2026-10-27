/* G20A experimental binary spatial partition, compiled only on request.
 * Offline node bounds describe static geometry, never actors. Runtime maps
 * remain authoritative: only unchanged class2 cells may become occluders.
 * All projection addresses here are GBA coordinates, not original NE addresses.
 */
extern const int16_t dok_bsp_bounds[6][8191][2];
extern const uint16_t dok_bsp_padding[6];
typedef struct {int16_t top,bottom;int32_t depth;} bsp_interval;
#ifdef DOK_RENDER_HW_BSP_REUSE_PROTOTYPE
static uint8_t *bsp_hidden,*bsp_reused;
static bsp_interval *bsp_solid;
#else
static uint8_t bsp_hidden[512];
static bsp_interval bsp_solid[120];
#endif
volatile uint32_t g_bsp_work[4]; /* nodes, hidden nodes, hidden cells, occluder faces */

#ifdef DOK_RENDER_HW_BSP_THUMB_PROTOTYPE
#define DOK_BSP_HOT __attribute__((target("thumb"),noinline,section(".rom.text")))
#elif defined(DOK_RENDER_WORLD_PLANES_PROTOTYPE) || defined(DOK_RENDER_HW_BSP_REUSE_PROTOTYPE)
#define DOK_BSP_HOT DOK_COLD
#else
#define DOK_BSP_HOT DOK_NOINLINE
#endif
#ifdef DOK_RENDER_HW_BSP_THUMB_PROTOTYPE
/* ARM7 Thumb has no UMULL; keep the wide interval products in ARM mode. */
static DOK_COLD int bsp_box(const dok_projection_camera *c,int x,int z,int sx,int sz,
#else
static DOK_BSP_HOT int bsp_box(const dok_projection_camera *c,int x,int z,int sx,int sz,
#endif
    int low,int high,unsigned padding) {
    /* Interval projection needs four reciprocal multiplies per node instead
     * of projecting all eight corners with software division. The margins
     * cover transform floors, quotient truncation and native Q15 projection. */
    extern const uint32_t dok_plane_reciprocal[16385];
    dok_camera_point p;
    dok_projection_transform(c,x*480,0,z*480,&p);
    int wx=sx*480+(int)padding,wz=sz*480+(int)padding;
    int ax=sar(wx*c->cosine,15),az=sar(wz*c->sine,15);
    int dx=-sar(wx*c->sine,15),dz=sar(wz*c->cosine,15);
    int lmin=p.lateral+(ax<0?ax:0)+(az<0?az:0)-3;
    int lmax=p.lateral+(ax>0?ax:0)+(az>0?az:0)+3;
    int near=p.depth+(dx<0?dx:0)+(dz<0?dz:0)-3;
    int far=p.depth+(dx>0?dx:0)+(dz>0?dz:0)+3;
    if(near<=0 || far+200>16384) return 0;
    int a=low-c->y,b=high-c->y;
    int left=119+(int)(((int64_t)lmin*150*dok_plane_reciprocal[(lmin<0?near:far)+200])>>31)-4;
    int right=119+(int)(((int64_t)lmax*150*dok_plane_reciprocal[(lmax>0?near:far)+200])>>31)+4;
    int top=71+(int)(((int64_t)a*180*dok_plane_reciprocal[(a<0?near:far)+200])>>31)-4;
    int bottom=71+(int)(((int64_t)b*180*dok_plane_reciprocal[(b>0?near:far)+200])>>31)+4;
    if(right<0 || left>239 || bottom<0 || top>143) return 1;
    left=clamp(left,0,239);right=clamp(right,0,239);
    top=clamp(top,0,143);bottom=clamp(bottom,0,143);
    for(int px=(left+1)&~1;px<=right;px+=2) {
        const int n=px>>1;
        if(bsp_solid[n].depth>=near || bsp_solid[n].top>top || bsp_solid[n].bottom<bottom) return 0;
    }
    return 1;
}
static DOK_BSP_HOT void bsp_occluder(const dok_renderer_face *original) {
    dok_renderer_face f=*original;
    if(f.x0>f.x1) {
        int n=f.x0;f.x0=f.x1;f.x1=n;n=f.top0;f.top0=f.top1;f.top1=n;
        n=f.bottom0;f.bottom0=f.bottom1;f.bottom1=n;
    }
    int span=f.x1-f.x0;if(span<=0 || f.x1<0 || f.x0>239) return;
    int far=(f.depth0>f.depth1?f.depth0:f.depth1)+2;
    int first=(clamp(f.x0,0,239)+1)&~1;
    int dt=(f.top1-f.top0)*65536/span,db=(f.bottom1-f.bottom0)*65536/span;
    int tq=f.top0*65536+(first-f.x0)*dt,bq=f.bottom0*65536+(first-f.x0)*db;
    for(int x=first;x<=clamp(f.x1,0,239);x+=2,tq+=dt*2,bq+=db*2) {
        int top=(tq>>16)+3,bottom=(bq>>16)-3;
        if(bottom<0 || top>143 || top>bottom) continue;
        top=clamp(top,0,143);bottom=clamp(bottom,0,143);
        unsigned n=(unsigned)x>>1;
        if(bsp_solid[n].top>bsp_solid[n].bottom) {
            bsp_solid[n].top=(int16_t)top;bsp_solid[n].bottom=(int16_t)bottom;bsp_solid[n].depth=far;
        } else if(top<=bsp_solid[n].bottom+1 && bottom+1>=bsp_solid[n].top) {
            if(top<bsp_solid[n].top) bsp_solid[n].top=(int16_t)top;
            if(bottom>bsp_solid[n].bottom) bsp_solid[n].bottom=(int16_t)bottom;
            if(far>bsp_solid[n].depth) bsp_solid[n].depth=far;
        }
    }
    ++g_bsp_work[3];
}
#ifdef DOK_RENDER_HW_BSP_LEAF_RAM_PROTOTYPE
static DOK_NOINLINE dok_renderer_status bsp_leaf(dok_renderer_workspace *w,const dok_runtime *r,
#else
static DOK_COLD dok_renderer_status bsp_leaf(dok_renderer_workspace *w,const dok_runtime *r,
#endif
    const dok_rom_assets *a,unsigned level,unsigned language,const dok_projection_camera *c,int x,int z) {
    uint16_t object,fc;int32_t inset;
    if(!object_at(r,DOK_WORLD_TMAP,x,z,&object) || !object_at(r,DOK_WORLD_FCMAP,x,z,&fc)) return DOK_RENDER_PROFILE;
    const dok_runtime_definition *d=&r->level->definitions[object];
    if(d->class_id!=2) return DOK_RENDER_OK;
    dok_renderer_status status=image(w,r,a,level,language,object);if(status) return status;
    const dok_renderer_image *im=&w->images[object];
    /* Masked walls never occlude a subtree, including opaque-looking columns. */
    if(im->flags&8) return DOK_RENDER_OK;
    int width=word(im->width*d->width_parameter),height=word(im->height*d->width_parameter);
    int y=word(-r->level->definitions[fc].height_parameter);
#ifdef DOK_RENDER_HW_BSP_REUSE_PROTOTYPE
    if(width<=0 || height<=0 || width>4096 || height>4096)return DOK_RENDER_PROFILE;
    bsp_reused[z*8+(x>>3)]|=(uint8_t)(1u<<(x&7));
#endif
    const int dx[4]={0,-1,0,1},dz[4]={-1,0,1,0};
    for(unsigned k=0;k<4;++k) {
        unsigned cls;uint16_t nf;
        if(!neighbor_class(r,x+dx[k],z+dz[k],&cls) || cls==2) continue;
#ifdef DOK_RENDER_HW_BSP_REUSE_PROTOTYPE
        int ex=c->x+sar(c->sine*200,15),ez=c->z-sar(c->cosine*200,15);
        if(width==480 && height==960 &&
           !(c->x>=x*480-4 && c->x<=x*480+width+4 && c->z>=z*480-4 && c->z<=z*480+width+4) &&
           !(ex>=x*480-4 && ex<=x*480+width+4 && ez>=z*480-4 && ez<=z*480+width+4)) {
            if((k==0 && ez>z*480+4) || (k==1 && ex>x*480+4) ||
               (k==2 && ez<z*480+width-4) || (k==3 && ex<x*480+width-4))continue;
        }
#endif
        if(!object_at(r,DOK_WORLD_FCMAP,x+dx[k],z+dz[k],&nf) || !field(r->level,nf,0x54,&inset)) return DOK_RENDER_PROFILE;
        int h=word(height-inset+1);if(h<=0) continue;
        int ax=x*480,az=z*480,bx=ax,bz=az;
        if(k==0) bx+=width;else if(k==1) az+=width;
        else if(k==2) {ax+=width;az+=width;bz+=width;} else {ax+=width;bx+=width;bz+=width;}
        unsigned before=w->face_count;dok_renderer_stats dummy={0};
        int16_t v0=0;
#ifdef DOK_RENDER_HW_BSP_REUSE_PROTOTYPE
        v0=word((int32_t)im->height*inset/960);
#endif
        status=plane(w,c,&dummy,object,ax,az,bx,bz,y,h,v0,0);
        if(status) return status;
        if(w->face_count>before) bsp_occluder(&w->faces[before]);
#ifndef DOK_RENDER_HW_BSP_REUSE_PROTOTYPE
        w->face_count=(uint16_t)before;
#endif
    }
    return DOK_RENDER_OK;
}
static DOK_BSP_HOT dok_renderer_status bsp_visit(dok_renderer_workspace *w,const dok_runtime *r,
    const dok_rom_assets *a,unsigned level,unsigned language,const dok_projection_camera *c,
    unsigned node,int x,int z,int sx,int sz,const int envelope[4]) {
    if(x>envelope[1] || z>envelope[3] || x+sx<=envelope[0] || z+sz<=envelope[2]) return DOK_RENDER_OK;
    ++g_bsp_work[0];unsigned view=level*2+language;
    const int16_t *b=dok_bsp_bounds[view][node];
    if(bsp_box(c,x,z,sx,sz,b[0],b[1],dok_bsp_padding[view])) {
        ++g_bsp_work[1];
        for(int cz=z;cz<z+sz;++cz) for(int cx=x;cx<x+sx;++cx) {
            bsp_hidden[cz*8+(cx>>3)]|=(uint8_t)(1u<<(cx&7));++g_bsp_work[2];
        }
        return DOK_RENDER_OK;
    }
#if defined(DOK_RENDER_HW_BSP_BLOCK4_PROTOTYPE) || defined(DOK_RENDER_HW_BSP_BLOCK2_PROTOTYPE)
#ifdef DOK_RENDER_HW_BSP_BLOCK4_PROTOTYPE
    const int leaf_side=4;
#else
    const int leaf_side=2;
#endif
    if(sx<=leaf_side && sz<=leaf_side) {
        for(int cz=z;cz<z+sz;++cz)for(int cx=x;cx<x+sx;++cx) {
            if(cx<envelope[0] || cx>envelope[1] || cz<envelope[2] || cz>envelope[3])continue;
            dok_renderer_status status=bsp_leaf(w,r,a,level,language,c,cx,cz);if(status)return status;
        }
        return DOK_RENDER_OK;
    }
#else
    if(sx==1 && sz==1) return bsp_leaf(w,r,a,level,language,c,x,z);
#endif
    int axis=sx>=sz,half=(axis?sx:sz)/2;
    int far_first=(axis?c->x:c->z)>((axis?x:z)+half)*480;
    unsigned children[2]={node+1,node+(unsigned)(sx*sz)};
    for(unsigned k=0;k<2;++k) {
        unsigned part=k^(unsigned)far_first;
        dok_renderer_status status=bsp_visit(w,r,a,level,language,c,children[part],
            x+(axis?(int)part*half:0),z+(axis?0:(int)part*half),axis?half:sx,axis?sz:half,envelope);
        if(status) return status;
    }
    return DOK_RENDER_OK;
}
static DOK_COLD dok_renderer_status bsp_prepare(dok_renderer_workspace *w,const dok_runtime *r,
    const dok_rom_assets *a,unsigned level,unsigned language,const dok_projection_camera *c) {
#ifdef DOK_RENDER_HW_BSP_REUSE_PROTOTYPE
    /* Geometry precedes rasterization. The interval payload is dead until
     * coverage_count is initialized, and can hold BSP scratch meanwhile. */
    _Static_assert(sizeof(w->covered)>=1984+3,"BSP geometry scratch");
    bsp_hidden=(uint8_t *)(((uintptr_t)w->covered+3)&~(uintptr_t)3);
    bsp_reused=bsp_hidden+512;bsp_solid=(bsp_interval *)(void *)(bsp_hidden+1024);
    zero(bsp_reused,512);
#endif
    zero(bsp_hidden,512);zero((void *)g_bsp_work,sizeof(g_bsp_work));
    for(unsigned x=0;x<120;++x) {bsp_solid[x].top=144;bsp_solid[x].bottom=-1;bsp_solid[x].depth=INT_MAX;}
    int envelope[4]={63,0,63,0};const int lateral[4]={-1,1,-6,6},depth[4]={-2,-2,7,7};
    for(unsigned k=0;k<4;++k) {
        int dx=sar((lateral[k]*c->cosine-depth[k]*c->sine)*480,15);
        int dz=sar((lateral[k]*c->sine+depth[k]*c->cosine)*480,15);
        int x=clamp((c->x+dx)/480,0,63),z=clamp((c->z+dz)/480,0,63);
        if(x<envelope[0]) envelope[0]=x;if(x>envelope[1]) envelope[1]=x;
        if(z<envelope[2]) envelope[2]=z;if(z>envelope[3]) envelope[3]=z;
    }
    return bsp_visit(w,r,a,level,language,c,0,0,0,64,64,envelope);
}
