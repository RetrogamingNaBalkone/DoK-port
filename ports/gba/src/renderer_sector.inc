/* Near-to-far segment BSP with closed-X, ceiling clip histories, one shared
 * visplane and deferred back-to-front dynamics. Original gameplay leases are
 * still published by build(); source alpha is consumed only by the final ARM
 * dynamic sampler, never by a per-row visibility/depth pass. */
extern int32_t dok_gba_plane_divide(int32_t,unsigned);
extern unsigned dok_gba_tiled_column(uint8_t *,const uint8_t *,unsigned,unsigned,unsigned,unsigned,unsigned);
#ifdef DOK_RENDER_SECTOR_HOT_PROTOTYPE
#define SECTOR_PLANE_CODE DOK_NOINLINE
#else
#define SECTOR_PLANE_CODE DOK_COLD
#endif
#if defined(DOK_RENDER_SECTOR_HOT_PROTOTYPE) && !defined(DOK_RENDER_SECTOR_BITMAP_PROTOTYPE) && !defined(DOK_RENDER_SECTOR_GPU_PROTOTYPE)
#define SECTOR_BOX_CODE DOK_NOINLINE
#else
#define SECTOR_BOX_CODE DOK_COLD
#endif
#ifdef DOK_RENDER_SECTOR_BITMAP_PROTOTYPE
extern unsigned dok_gba_bitmap_column(uint8_t *,const uint8_t *,unsigned,unsigned,unsigned,unsigned,unsigned);
#define sector_column dok_gba_bitmap_column
#define sector_masked_column dok_gba_bitmap_masked_column
#else
#define sector_column dok_gba_tiled_column
#define sector_masked_column dok_gba_tiled_masked_column
#endif
static int sector_div(int n,unsigned d) {return d && d<=16384?dok_gba_plane_divide(n,d):n/(int)d;}
static int sector_y(int height,unsigned z) {
    return scale_y(sar(mul(height-sector_camera->y,sector_div(6553600,z+200)),15)+79);
}
static int sector_z(const sector_ray *r,unsigned axis,int value) {
    int origin=axis?r->oz:r->ox,inv=axis?r->iz:r->ix;
    return (int)(((int64_t)((int64_t)value*65536-origin)*inv)>>29);
}
static void sector_range(uint32_t bits[4],unsigned first,unsigned last) {
    unsigned a=first>>5,b=last>>5;uint32_t lo=UINT32_MAX<<(first&31),hi=UINT32_MAX>>(31-(last&31));
    if(a==b)bits[a]|=lo&hi;else {bits[a]|=lo;for(unsigned k=a+1;k<b;++k)bits[k]=UINT32_MAX;bits[b]|=hi;}
}
static int sector_record(unsigned x,unsigned near,unsigned far,unsigned top,unsigned rim) {
    unsigned n=sector->counts[x];
    if(top<=sector->top[x])return 1;
    if(n==64 || near>32767 || far>32767){++g_sector_work[11];return 0;}
    sector_history_rows[x][n]=(sector_history){(uint16_t)(near|(rim?32768:0)),(uint16_t)far};
    sector->history_top[x][n]=(uint8_t)top;sector->counts[x]=(uint8_t)(n+1);sector->top[x]=(uint8_t)top;
    if(n+1>g_sector_work[7])g_sector_work[7]=n+1;return 1;
}
static DOK_COLD dok_renderer_status sector_begin(dok_renderer_workspace *w,const dok_runtime *r,const dok_rom_assets *a,
    unsigned level,unsigned language,const dok_projection_camera *c) {
    sector_active=sector_emitting=sector_dynamic=0;sector_ceiling_object=UINT16_MAX;
#ifdef DOK_RENDER_SECTOR_GPU_PROTOTYPE
    g_sector_gpu=0;
#endif
    for(unsigned k=0;k<12;++k)g_sector_work[k]=0;for(unsigned k=0;k<5;++k)g_sector_ticks[k]=0;
    unsigned native=g_hw_tile_active;
#ifdef DOK_RENDER_SECTOR_BITMAP_PROTOTYPE
    /* Every declined scene uses the complete legacy Mode4 renderer. */
    g_hw_tile_active=0;
#endif
    if(!native || c->y<=-560)return DOK_RENDER_OK;
    extern unsigned dok_gba_geometry_equal(const void *,const void *,unsigned);
    if(!dok_gba_geometry_equal(r->maps+4096,r->level->maps+4096,8192))return DOK_RENDER_OK;
    for(unsigned id=0;id<r->object_count;++id) {
        int inset=dok_static_insets[static_view][id];
        if(inset && inset!=10 && inset!=400)return DOK_RENDER_OK;
        if(inset && r->level->definitions[id].height_parameter)return DOK_RENDER_OK;
        if(inset==400 && sector_ceiling_object==UINT16_MAX)sector_ceiling_object=id;
        if(r->level->definitions[id].class_id!=2)continue;
        if((r->objects[id].flags&0x800)||r->objects[id].visual_resource)return DOK_RENDER_OK;
        unsigned lease=w->image_known[id];dok_renderer_status s=image(w,r,a,level,language,(uint16_t)id);if(s)return s;w->image_known[id]=(uint8_t)lease;
        const dok_renderer_image *im=&w->images[id];int scale=r->level->definitions[id].width_parameter;
        if(im->width*scale!=480 || im->height*scale!=960 || (im->flags&8) || !im->columns)return DOK_RENDER_OK;
    }
    sector=(void *)(((uintptr_t)w->covered+3)&~(uintptr_t)3);sector_history_rows=(void *)w->surfaces;
    sector_camera=c;sector_active=1;g_sector_work[0]=1;
#ifdef DOK_RENDER_SECTOR_BITMAP_PROTOTYPE
    g_hw_tile_active=1;
#endif
    return DOK_RENDER_OK;
}
static DOK_COLD void sector_prepare(void) {
    const dok_projection_camera *c=sector_camera;
    zero(sector->roof,sizeof(sector->roof));zero(sector->closed,sizeof(sector->closed));
    zero(sector->top,sizeof(sector->top));zero(sector->counts,sizeof(sector->counts));
    for(unsigned x=0;x<120;++x) {
        sector->wall_z[x]=65535;sector->wall_first[x]=sector->wall_last[x]=0;
        int lateral=(int)(x*2)*4/3-159;sector_ray *r=&sector->ray[x];
        r->ox=c->x*65536+c->cosine*lateral*2;r->oz=c->z*65536+c->sine*lateral*2;
        r->dx=-c->sine+c->cosine*lateral/200;r->dz=c->cosine+c->sine*lateral/200;
        r->ix=r->dx?268435456/r->dx:0;r->iz=r->dz?268435456/r->dz:0;
    }
    for(unsigned y=0;y<128;++y){int dy=(int)y*5/4-79,z=dy<0?(-561-c->y)*200/dy-200:-1;
        sector->ceiling_depth[y]=(uint16_t)(z>=0 && z<32768?z:65535);}
}
static SECTOR_BOX_CODE int sector_box(const uint16_t bounds[4],unsigned *first,unsigned *last) {
    int x0=bounds[0],z0=bounds[1],x1=bounds[2],z1=bounds[3];
    if(x0>x1 || z0>z1)return 0;
    if(x0<g_hw_bg_envelope[0]*480)x0=g_hw_bg_envelope[0]*480;
    if(z0<g_hw_bg_envelope[2]*480)z0=g_hw_bg_envelope[2]*480;
    if(x1>(g_hw_bg_envelope[1]+1)*480)x1=(g_hw_bg_envelope[1]+1)*480;
    if(z1>(g_hw_bg_envelope[3]+1)*480)z1=(g_hw_bg_envelope[3]+1)*480;
    if(x0>x1 || z0>z1)return 0;
    const dok_projection_camera *c=sector_camera;dok_camera_point p;dok_projection_transform(c,x0,0,z0,&p);
    int wx=x1-x0,wz=z1-z0,ax=sar(wx*c->cosine,15),az=sar(wz*c->sine,15),dx=-sar(wx*c->sine,15),dz=sar(wz*c->cosine,15);
    int near=p.depth+(dx<0?dx:0)+(dz<0?dz:0)-3,far=p.depth+(dx>0?dx:0)+(dz>0?dz:0)+3;
    if(far<0)return 0;
    int lo=0,hi=239;
    if(near>0) {
        int lmin=p.lateral+(ax<0?ax:0)+(az<0?az:0)-3,lmax=p.lateral+(ax>0?ax:0)+(az>0?az:0)+3;
        lo=119+sector_div(lmin*150,(unsigned)((lmin<0?near:far)+200))-4;
        hi=119+sector_div(lmax*150,(unsigned)((lmax>0?near:far)+200))+4;
        if(hi<0 || lo>239)return 0;
    }
    *first=(unsigned)clamp(lo,0,239)/2;*last=(unsigned)clamp(hi+1,0,239)/2;
    unsigned a=*first>>5,b=*last>>5;uint32_t lm=UINT32_MAX<<(*first&31),hm=UINT32_MAX>>(31-(*last&31));
    if(a==b)return (sector->closed[a]&(lm&hm))!=(lm&hm);
    if((sector->closed[a]&lm)!=lm || (sector->closed[b]&hm)!=hm)return 1;
    for(unsigned k=a+1;k<b;++k)if(sector->closed[k]!=UINT32_MAX)return 1;
    return 0;
}
static DOK_NOINLINE void sector_static_face(dok_renderer_workspace *w,const dok_renderer_face *face,dok_renderer_stats *stats) {
    const sector_atom *a=sector_current_atom;unsigned axis=a->ax==a->bx?0:1;
    int fixed=axis?a->az:a->ax,start=axis?a->ax:a->az,stop=axis?a->bx:a->bz;
    const dok_renderer_image *im=&w->images[a->object];
    int first=(clamp((face->x0<face->x1?face->x0:face->x1)-3,0,239)+1)&~1;
    int last=clamp((face->x0>face->x1?face->x0:face->x1)+3,0,239);
    extern const uint8_t dok_prelit_bank[];unsigned wrote=0;
    for(int xx=first;xx<=last;xx+=2) {
        unsigned x=(unsigned)xx/2;if(sector->closed[x>>5]&(1u<<(x&31)))continue;
        const sector_ray *r=&sector->ray[x];if(!(axis?r->iz:r->ix))continue;
        int z=sector_z(r,axis,fixed);if(z<0 || z>32767)continue;
        int coordinate=sar(add(axis?r->ox:r->oz,mul(axis?r->dx:r->dz,z*2)),16);
        int phase=stop>start?coordinate-start:start-coordinate;
        if(phase< -1 || phase>481)continue;phase=clamp(phase,0,480);
        unsigned u=(unsigned)(phase*(im->width-1)/480);
        int top=sector_y(a->y-a->height,(unsigned)z),bottom=sector_y(a->y,(unsigned)z);
        int begin=clamp(top,0,127),end=clamp(bottom,0,127);if(begin<sector->top[x])begin=sector->top[x];
        if(top<=127 && bottom>=0 && begin<=end) {
            unsigned v=(unsigned)clamp(a->v0,0,im->height-1);
            unsigned step=bottom>top?(unsigned)sector_div((im->height-1-v)<<16,(unsigned)(bottom-top)):0;
            unsigned row=light_index(z),off=prelit_view[im->resource_id*16+row];
            sector_column(scene_pointer(w->frame,(unsigned)xx,(unsigned)begin),dok_prelit_bank+off+u*im->height,
                (v<<16)+(unsigned)(begin-top)*step,step,(unsigned)(end-begin+1),(unsigned)begin&7,g_hw_tile_black);
            COVER_MARK((unsigned)xx,(unsigned)begin,(unsigned)end);COVER_COUNT(w,(unsigned)xx)=128;
            stats->pixels_written+=(unsigned)(end-begin+1);wrote=1;
        }
        if(a->kind) {
            ++g_sector_work[5];
            if(bottom>=0 && bottom<128)sector_record(x,(unsigned)z,(unsigned)z,(unsigned)bottom+1,1);
        } else {
            ++g_sector_work[4];sector->closed[x>>5]|=1u<<(x&31);sector->wall_z[x]=(uint16_t)z;
            sector->wall_first[x]=(uint8_t)clamp(top,0,127);sector->wall_last[x]=(uint8_t)clamp(bottom,0,127);
        }
    }
    if(wrote)++stats->faces_drawn;
}
static DOK_COLD dok_renderer_status sector_atom_draw(dok_renderer_workspace *w,const dok_runtime *r,const dok_rom_assets *assets,
    unsigned level,unsigned language,dok_renderer_stats *stats,const sector_atom *a) {
    int x=a->cell%64,z=a->cell/64;
    if(x<g_hw_bg_envelope[0] || x>g_hw_bg_envelope[1] || z<g_hw_bg_envelope[2] || z>g_hw_bg_envelope[3] || (a->kind && a->height<=10))return DOK_RENDER_OK;
    const dok_projection_camera *c=sector_camera;int ex=c->x+sar(c->sine*200,15),ez=c->z-sar(c->cosine*200,15),x0=x*480,z0=z*480;
    int inside=(c->x>=x0-4 && c->x<=x0+484 && c->z>=z0-4 && c->z<=z0+484)||(ex>=x0-4 && ex<=x0+484 && ez>=z0-4 && ez<=z0+484);
    if(!inside && ((a->side==0 && ez>z0+4)||(a->side==1 && ex>x0+4)||(a->side==2 && ez<z0+476)||(a->side==3 && ex<x0+476)))return DOK_RENDER_OK;
    dok_renderer_image saved=w->images[a->object];dok_renderer_status status;
    if(a->kind) {
        unsigned lease=w->surface_known[a->object][2];status=surface_image(w,r,assets,level,language,a->object,2);if(status)return status;
        w->surface_known[a->object][2]=(uint8_t)lease;const dok_image_view *im=&w->surface_images[a->object][2];
        extern const uint16_t dok_side_resources[6][256];extern const uint8_t dok_prelit_bank[];
        unsigned rid=dok_side_resources[static_view][a->object],off=prelit_view[rid*16];
        w->images[a->object]=(dok_renderer_image){im->pixels,dok_prelit_bank+off,0,im->width,im->height,im->stored_flags,(uint16_t)rid};
    } else {unsigned lease=w->image_known[a->object];status=image(w,r,assets,level,language,a->object);if(status)return status;w->image_known[a->object]=(uint8_t)lease;}
    sector_current_atom=a;++g_sector_work[3];
    status=plane(w,c,stats,a->object,a->ax,a->az,a->bx,a->bz,a->y,a->height,a->v0,0);
    if(a->kind)w->images[a->object]=saved;return status;
}
static SECTOR_PLANE_CODE void sector_leaf_draw(const sector_leaf *leaf) {
    if(!leaf->count)return;
    unsigned first,last;if(!sector_box(leaf->bounds,&first,&last))return;
    int bounds[4]={leaf->bounds[0],leaf->bounds[1],leaf->bounds[2],leaf->bounds[3]};
    if(bounds[0]<g_hw_bg_envelope[0]*480)bounds[0]=g_hw_bg_envelope[0]*480;
    if(bounds[1]<g_hw_bg_envelope[2]*480)bounds[1]=g_hw_bg_envelope[2]*480;
    if(bounds[2]>(g_hw_bg_envelope[1]+1)*480)bounds[2]=(g_hw_bg_envelope[1]+1)*480;
    if(bounds[3]>(g_hw_bg_envelope[3]+1)*480)bounds[3]=(g_hw_bg_envelope[3]+1)*480;
    for(unsigned x=first;x<=last;++x) {
        if(sector->closed[x>>5]&(1u<<(x&31)))continue;
        const sector_ray *r=&sector->ray[x];int near=0,far=32767,valid=1;
        for(unsigned axis=0;axis<2;++axis) {
            int inv=axis?r->iz:r->ix,origin=axis?r->oz:r->ox;
            if(!inv){if(origin<(int64_t)bounds[axis]*65536 || origin>(int64_t)bounds[axis+2]*65536)valid=0;continue;}
            int a=sector_z(r,axis,bounds[axis]),b=sector_z(r,axis,bounds[axis+2]);if(a>b){int t=a;a=b;b=t;}
            if(a>near)near=a;if(b<far)far=b;
        }
        if(!valid || far<near || far<0)continue;
        int top=sector_y(-561,(unsigned)near),bottom=sector_y(-561,(unsigned)far);
        if(bottom<0 || top>127)continue;
        int begin=clamp(top,0,127),end=clamp(bottom,0,127);if(begin<sector->top[x])begin=sector->top[x];
        if(begin>end)continue;
        sector_range(sector->roof[x],(unsigned)begin,(unsigned)end);
        sector_record(x,(unsigned)near,(unsigned)far,(unsigned)end+1,0);++g_sector_work[6];
    }
}
static DOK_COLD dok_renderer_status sector_visit(dok_renderer_workspace *w,const dok_runtime *r,const dok_rom_assets *a,
    unsigned level,unsigned language,dok_renderer_stats *stats,const sector_view *view,int index) {
    unsigned first,last;++g_sector_work[1];
    const uint16_t *bounds=index<0?view->leaves[-index-1].bounds:view->nodes[index].bounds;
    if(!sector_box(bounds,&first,&last)){++g_sector_work[2];return DOK_RENDER_OK;}
    if(index<0){sector_leaf_draw(&view->leaves[-index-1]);return DOK_RENDER_OK;}
    const sector_node *n=&view->nodes[index];const dok_projection_camera *c=sector_camera;
    int eye=n->axis?c->z-sar(c->cosine*200,15):c->x+sar(c->sine*200,15);unsigned side=eye>n->fixed;
    dok_renderer_status status=sector_visit(w,r,a,level,language,stats,view,n->child[side]);if(status)return status;
    for(unsigned k=0;k<n->count;++k){status=sector_atom_draw(w,r,a,level,language,stats,&view->atoms[view->ids[n->first+k]]);if(status)return status;}
    return sector_visit(w,r,a,level,language,stats,view,n->child[side^1]);
}
typedef struct {uint8_t *dst;const uint8_t *pixels,*lut;const uint32_t *visible;int wx,wz,dx,dz;uint32_t *coverage;unsigned mask,count,width,height,black;} sector_plane_span;
extern unsigned dok_gba_sector_plane(const sector_plane_span *);
static SECTOR_PLANE_CODE void sector_ceiling_draw(dok_renderer_workspace *w,dok_renderer_stats *stats) {
    if(sector_ceiling_object==UINT16_MAX)return;
    const dok_image_view *im=&w->surface_images[sector_ceiling_object][1];const dok_projection_camera *c=sector_camera;
    for(unsigned y=0;y<128;++y) {
        unsigned z=sector->ceiling_depth[y];if(z==65535)continue;
        unsigned bit=1u<<(y&31),any=0;for(unsigned x=0;x<120;++x)any|=sector->roof[x][y>>5]&bit;if(!any)continue;
        int distance=(int)z+200,lateral=-distance*52101,step=distance*65536/75;
        int wx=c->x*65536/480+(int)(((int64_t)lateral*c->cosine)>>15)/480-c->sine*(int)z*2/480;
        int wz=c->z*65536/480+(int)(((int64_t)lateral*c->sine)>>15)/480+c->cosine*(int)z*2/480;
        int dx=(int)(((int64_t)step*c->cosine)>>15)/480,dz=(int)(((int64_t)step*c->sine)>>15)/480;
        sector_plane_span span={scene_pointer(w->frame,0,y),im->pixels,w->lights+light_index((int)z)*256,&sector->roof[0][y>>5],
            wx,wz,dx,dz,&coverage_bits[0][y>>5],bit,120,im->width-1,im->height-1,g_hw_tile_black};
        unsigned n=dok_gba_sector_plane(&span);stats->pixels_written+=n;g_sector_work[9]+=n;
    }
}
static DOK_NOINLINE unsigned sector_dynamic_column(dok_renderer_workspace *w,unsigned xx,unsigned begin,unsigned end,
    const uint8_t *pixels,unsigned v,unsigned step,unsigned masked) {
    unsigned x=xx/2;int z=sector->dx0;
    if(sector->dx0!=sector->dx1) {
        int i=(int)xx-sector->dx_start,n0=sector->dq0*(sector->dx-i),n1=sector->dq1*i;
        z=(sector->dx0*n0+sector->dx1*n1)/(n0+n1);
    }
    if(z<0)z=0;
    unsigned cutoff=0;
    for(unsigned rank=sector->counts[x];rank>0;--rank) {
        const sector_history *h=&sector_history_rows[x][rank-1];if((h->near&32767)>(unsigned)z)continue;
        cutoff=sector->history_top[x][rank-1];
        if((unsigned)z<h->far) {
            int y=sector_y(-561,(unsigned)z)+1;cutoff=(unsigned)clamp(y,0,128);
            if(rank>1 && cutoff<sector->history_top[x][rank-2])cutoff=sector->history_top[x][rank-2];
        }
        break;
    }
    if(begin<cutoff){v+=(cutoff-begin)*step;begin=cutoff;}
    if(begin>end)return 0;
    unsigned first[2],last[2],runs=0;
    if((unsigned)z>=sector->wall_z[x] && sector->wall_first[x]<=end && sector->wall_last[x]>=begin) {
        if(begin<sector->wall_first[x]){first[runs]=begin;last[runs++]=sector->wall_first[x]-1;}
        if(end>sector->wall_last[x]){first[runs]=sector->wall_last[x]+1;last[runs++]=end;}
    } else {first[0]=begin;last[0]=end;runs=1;}
    unsigned result=0;uint32_t bits[4]={0,0,0,0};
    for(unsigned k=0;k<runs;++k) {
        unsigned count=last[k]-first[k]+1,pos=v+(first[k]-begin)*step;
        if(masked) {
            struct {uint8_t *dst;const uint8_t *pixels;unsigned v,step,count;uint32_t *bits;unsigned mask,tile_y;} span={
                scene_pointer(w->frame,xx,first[k]),pixels,pos,step,count,&bits[first[k]>>5],1u<<(first[k]&31),first[k]&7};
            extern unsigned sector_masked_column(const void *);result+=sector_masked_column(&span);
        } else {
            result+=sector_column(scene_pointer(w->frame,xx,first[k]),pixels,pos,step,count,first[k]&7,g_hw_tile_black);
            sector_range(bits,first[k],last[k]);
        }
    }
    for(unsigned k=0;k<4;++k)coverage_bits[x][k]|=bits[k];COVER_COUNT(w,xx)=128;
#ifdef DOK_RENDER_SECTOR_GPU_PROTOTYPE
    if(g_sector_gpu_trace && g_sector_gpu)for(unsigned k=0;k<4;++k)((uint32_t *)(void *)g_sector_gpu->texture)[x*4+k]|=bits[k];
#endif
    ++g_sector_work[8];g_sector_work[10]+=result&65535;return result;
}
#ifdef DOK_RENDER_SECTOR_BITMAP_PROTOTYPE
#include "renderer_sector_bitmap.inc"
#endif
#ifdef DOK_RENDER_SECTOR_GPU_PROTOTYPE
#include "renderer_sector_gpu.inc"
#endif
static DOK_COLD dok_renderer_status sector_draw(dok_renderer_workspace *w,const dok_runtime *r,const dok_rom_assets *a,
    unsigned level,unsigned language,const dok_projection_camera *c,dok_renderer_stats *stats) {
    (void)c;uint32_t stamp=g_render_work[0]?render_ticks():0;sector_prepare();
#ifdef DOK_RENDER_SECTOR_GPU_PROTOTYPE
    sector_gpu_begin(w,r);
#endif
    if(sector_ceiling_object!=UINT16_MAX){unsigned lease=w->surface_known[sector_ceiling_object][1];
        dok_renderer_status s=surface_image(w,r,a,level,language,sector_ceiling_object,1);if(s)return s;w->surface_known[sector_ceiling_object][1]=(uint8_t)lease;}
    if(g_render_work[0])g_sector_ticks[0]+=render_ticks()-stamp;
    stamp=g_render_work[0]?render_ticks():0;sector_emitting=1;
    dok_renderer_status status=sector_visit(w,r,a,level,language,stats,&dok_sector_views[static_view],0);sector_emitting=0;if(status)return status;
    if(g_sector_work[11])return DOK_RENDER_CAPACITY;
    if(g_render_work[0])g_sector_ticks[1]+=render_ticks()-stamp;
    stamp=g_render_work[0]?render_ticks():0;sector_ceiling_draw(w,stats);if(g_render_work[0])g_sector_ticks[2]+=render_ticks()-stamp;
#ifdef DOK_RENDER_SECTOR_BITMAP_PROTOTYPE
    stamp=g_render_work[0]?render_ticks():0;sector_bitmap_background(w,r);if(g_render_work[0])g_sector_ticks[4]+=render_ticks()-stamp;
#endif
    stamp=g_render_work[0]?render_ticks():0;
#ifdef DOK_RENDER_SECTOR_GPU_PROTOTYPE
    if(sector_gpu_dynamics(w,r)){if(g_render_work[0])g_sector_ticks[3]+=render_ticks()-stamp;w->surface_count=0;return DOK_RENDER_OK;}
    if(g_sector_gpu_trace)zero(g_sector_gpu->texture,1920);
#endif
    sector_dynamic=1;
    for(unsigned rank=0;rank<w->face_count;++rank) {
        const dok_renderer_face *f=&w->faces[face_order[rank]];
        sector->dx_start=f->x0<f->x1?f->x0:f->x1;sector->dx=f->x1>f->x0?f->x1-f->x0:f->x0-f->x1;
        sector->dx0=f->x0<f->x1?f->depth0:f->depth1;sector->dx1=f->x0<f->x1?f->depth1:f->depth0;
        sector->dq0=(6553600/(sector->dx0+200))>>8;sector->dq1=(6553600/(sector->dx1+200))>>8;
        if(sector->dq0<1)sector->dq0=1;if(sector->dq1<1)sector->dq1=1;
        raster(w,r,f,w->lights,stats,-1);
    }
    sector_dynamic=0;if(g_render_work[0])g_sector_ticks[3]+=render_ticks()-stamp;
    w->surface_count=0;return DOK_RENDER_OK;
}
