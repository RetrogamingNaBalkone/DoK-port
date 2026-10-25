#include "dok/renderer.h"
#include "dok/image.h"
#include <limits.h>
#ifdef DOK_RENDER_TRACE
#include <stdio.h>
#define PROFILE_FAIL() do {fprintf(stderr,"renderer profile line %d\n",__LINE__);return DOK_RENDER_PROFILE;} while(0)
#else
#define PROFILE_FAIL() return DOK_RENDER_PROFILE
#endif

static void zero(void *p,size_t n) {uint8_t *b=p;while(n--) *b++=0;}
static int32_t bits(uint32_t v) {return v<=INT32_MAX?(int32_t)v:-1-(int32_t)~v;}
static int16_t word(int32_t v) {uint16_t n=(uint16_t)v;return n<=32767?(int16_t)n:(int16_t)(-1-(int16_t)(65535u-n));}
static int32_t sar(int32_t v,unsigned n) {uint32_t b=(uint32_t)v;return bits((b>>n)|(v<0?(UINT32_MAX<<(32-n)):0));}
static int32_t mul(int32_t a,int32_t b) {return bits((uint32_t)a*(uint32_t)b);}
static int32_t add(int32_t a,int32_t b) {return bits((uint32_t)a+(uint32_t)b);}
static int32_t sub(int32_t a,int32_t b) {return bits((uint32_t)a-(uint32_t)b);}
static int32_t scale_x(int32_t x) {return x>=0?x*3/4:-(((-x)*3+3)/4);}
static int32_t scale_y(int32_t y) {return y>=0?y*9/10:-(((-y)*9+9)/10);}
static int clamp(int x,int a,int b) {return x<a?a:(x>b?b:x);}
static int field(const dok_runtime_level *l,uint16_t index,unsigned offset,int32_t *out) {
    const dok_runtime_definition *d;size_t j;
    if(index>=l->object_count) return 0;d=&l->definitions[index];
    for(j=0;j<d->field_count;++j) {
        const dok_runtime_field *f=&l->fields[d->field_begin+j];
        if(f->native_offset==offset && f->kind<=DOK_LEVEL_CHAR) {*out=f->value.number;return 1;}
    }
    return 0;
}
static int object_at(const dok_runtime *r,unsigned plane,int x,int z,uint16_t *out) {
    const dok_runtime_level *l=r->level;unsigned side=l->settings[DOK_LEVEL_WIDTH];uint16_t n;
    x=clamp(x,0,(int)side-1);z=clamp(z,0,(int)side-1);
    n=l->glyph_index[r->maps[plane*l->cells+(unsigned)z*side+(unsigned)x]];
    if(!n || n>r->object_count) return 0;*out=(uint16_t)(n-1);return 1;
}
static int neighbor_class(const dok_runtime *r,int x,int z,unsigned *out) {
    uint16_t n;if(!object_at(r,DOK_WORLD_TMAP,x,z,&n)) return 0;*out=r->level->definitions[n].class_id;return 1;
}
static dok_renderer_status image(dok_renderer_workspace *w,const dok_runtime *r,const dok_rom_assets *a,
    unsigned level,unsigned language,uint16_t object) {
    uint16_t id;dok_rom_resource resource;dok_image_view v;const dok_runtime_definition *d=&r->level->definitions[object];
    if(w->image_known[object]) return DOK_RENDER_OK;
    if(dok_rom_binding(&id,a,level,language,object,DOK_ROM_SLOT_SPRITE)!=DOK_ROM_OK ||
       dok_rom_resource_at(&resource,a,id,language)!=DOK_ROM_OK ||
       dok_image_open(resource.data,resource.size,&v)!=DOK_ASSET_OK) return DOK_RENDER_RESOURCE;
    /* G05 never mutates shared resource images. Initial crop index0 produces
     * the raw pixels for the checked Sprite shift modes; animated/static-state
     * updates are deliberately outside this renderer's contract. */
    if(v.width>256 || v.height>512 || !v.width || !v.height) PROFILE_FAIL();
    w->images[object].pixels=v.pixels;w->images[object].width=v.width;w->images[object].height=v.height;
    w->images[object].flags=(uint16_t)(v.stored_flags|d->sprite_flags);w->images[object].resource_id=id;
    w->image_known[object]=1;return DOK_RENDER_OK;
}
dok_renderer_status dok_renderer_camera_from_runtime(dok_projection_camera *out,const dok_runtime *r) {
    dok_projection_camera c;const dok_runtime_actor *a;uint16_t index;
    if(!out || !r || !r->level || !r->player_index || r->player_index>r->object_count) return DOK_RENDER_ARGUMENT;
    index=(uint16_t)(r->player_index-1);a=dok_runtime_actor_at(r,index);
    if(!a || a->heading!=90 || !dok_runtime_position(r,index,&c.x,&c.y,&c.z)) PROFILE_FAIL();
    c.sine=32768;c.cosine=0;c.distance=200;c.scale=8;c.focal=4096;c.center_x=159;c.center_y=79;
    *out=c;return DOK_RENDER_OK;
}
dok_renderer_status dok_renderer_palette(uint8_t out[768],const dok_rom_assets *a,unsigned level,unsigned language) {
    uint16_t id;dok_rom_resource resource;unsigned i;
    if(!out || !a) return DOK_RENDER_ARGUMENT;
    if(dok_rom_binding(&id,a,level,language,DOK_ROM_GLOBAL_OBJECT,DOK_ROM_SLOT_PALETTE)!=DOK_ROM_OK ||
       dok_rom_resource_at(&resource,a,id,language)!=DOK_ROM_OK || resource.size!=768) return DOK_RENDER_RESOURCE;
    for(i=0;i<768;++i) {if(resource.data[i]>63) return DOK_RENDER_RESOURCE;}
    for(i=0;i<768;++i) out[i]=resource.data[i];return DOK_RENDER_OK;
}
/* Depth0 near clipping uses original low32 numerator and signed IDIV. */
static int clip_endpoint(dok_camera_point *a,const dok_camera_point *b,int32_t *u,int32_t other_u) {
    int32_t divisor=sub(b->depth,a->depth),num=mul(sub(a->lateral,b->lateral),b->depth);
    int32_t unum=mul(sub(other_u,*u),-a->depth);
    if(!divisor || (num==INT32_MIN && divisor==-1) || (unum==INT32_MIN && divisor==-1)) return 0;
    a->lateral=add(num/divisor,b->lateral);*u=add(*u,unum/divisor);a->depth=0;return 1;
}
static dok_renderer_status plane(dok_renderer_workspace *w,const dok_projection_camera *c,dok_renderer_stats *s,
    uint16_t object,int32_t x0,int32_t z0,int32_t x1,int32_t z1,int32_t y,int32_t height,
    int16_t v0,int centered) {
    dok_camera_point a,b;dok_screen_point at,ab,bt,bb;dok_renderer_face f;int32_t u0=0,u1=w->images[object].width-1;
    dok_projection_transform(c,x0,y-height,z0,&a);dok_projection_transform(c,x1,y-height,z1,&b);
    if(a.depth<=0 && b.depth<=0) {++s->faces_rejected;return DOK_RENDER_OK;}
    if(a.depth<0) {if(!clip_endpoint(&a,&b,&u0,u1)) return DOK_RENDER_PROJECTION;++s->near_clipped;}
    else if(b.depth<0) {if(!clip_endpoint(&b,&a,&u1,u0)) return DOK_RENDER_PROJECTION;++s->near_clipped;}
    if(!dok_projection_project(c,&a,&at) || !dok_projection_project(c,&b,&bt)) return DOK_RENDER_PROJECTION;
    a.vertical=add(a.vertical,height);b.vertical=add(b.vertical,height);
    if(!dok_projection_project(c,&a,&ab) || !dok_projection_project(c,&b,&bb)) return DOK_RENDER_PROJECTION;
    /* The G05 profile bounds scalar projections before multiplies in mapping
     * and affine scan setup. Extreme probes fail rather than overflow C. */
    if(at.x< -8192 || at.x>8192 || bt.x< -8192 || bt.x>8192 || at.y< -8192 || at.y>8192 ||
       ab.y< -8192 || ab.y>8192 || bt.y< -8192 || bt.y>8192 || bb.y< -8192 || bb.y>8192) PROFILE_FAIL();
    if(at.x==bt.x || (at.x<0 && bt.x<0) || (at.x>319 && bt.x>319) ||
       (ab.y<0 && bb.y<0) || (at.y>159 && bt.y>159)) {++s->faces_rejected;return DOK_RENDER_OK;}
    f.x0=scale_x(at.x);f.x1=scale_x(bt.x);f.top0=scale_y(at.y);f.top1=scale_y(bt.y);
    f.bottom0=scale_y(ab.y);f.bottom1=scale_y(bb.y);f.depth0=a.depth;f.depth1=b.depth;
    f.u0=u0;f.u1=u1;f.key=sar(add(a.depth,b.depth),1);
    if(centered) f.key-=sar(centered,2);
    if(f.key<0) f.key=0;f.image_index=object;f.v0=v0;
    if(f.x0==f.x1) {++s->faces_rejected;return DOK_RENDER_OK;}
    if(w->face_count>=DOK_RENDER_FACE_CAPACITY) return DOK_RENDER_CAPACITY;
    w->faces[w->face_count++]=f;++s->faces_built;return DOK_RENDER_OK;
}
static dok_renderer_status build(dok_renderer_workspace *w,const dok_runtime *r,const dok_rom_assets *a,
    unsigned level,unsigned language,const dok_projection_camera *c,dok_renderer_stats *s) {
    const dok_runtime_level *l=r->level;int side=l->settings[DOK_LEVEL_WIDTH],cell=l->settings[DOK_LEVEL_CELL_SIDE];
    int xmin=clamp((c->x-7*cell)/cell,0,side-1),xmax=clamp((c->x+2*cell)/cell,0,side-1);
    int zmin=clamp((c->z-6*cell)/cell,0,side-1),zmax=clamp((c->z+6*cell)/cell,0,side-1),x,z;
    for(z=zmin;z<=zmax;++z) for(x=xmin;x<=xmax;++x) {
        uint16_t object,fc;const dok_runtime_definition *d;dok_renderer_status status;int32_t width,height,y;
        unsigned north,south,west,east;int32_t x0=x*cell,z0=z*cell;
        ++s->cells_considered;
        if(!object_at(r,DOK_WORLD_TMAP,x,z,&object) || !object_at(r,DOK_WORLD_FCMAP,x,z,&fc)) PROFILE_FAIL();
        d=&l->definitions[object];
        if(d->class_id!=2 && d->class_id!=4 && d->class_id!=7) {
            if(d->class_id==3 || d->class_id==9 || d->class_id==10 || d->class_id==6) ++s->skipped_billboards;
            continue;
        }
        if(d->flags&0x800) PROFILE_FAIL(); /* animated geometry is G11 */
        if(d->class_id==7 && r->objects[object].state.door.parameter_5c!=l->initial_objects[object].state.door.parameter_5c) PROFILE_FAIL();
        status=image(w,r,a,level,language,object);if(status) return status;
        width=word((int32_t)w->images[object].width*d->width_parameter);
        height=word((int32_t)w->images[object].height*d->width_parameter);
        y=word(-(int32_t)l->definitions[fc].height_parameter);
        if(width<=0 || height<=0 || width>4096 || height>4096) PROFILE_FAIL();
        if(!neighbor_class(r,x,z-1,&north) || !neighbor_class(r,x,z+1,&south) ||
           !neighbor_class(r,x-1,z,&west) || !neighbor_class(r,x+1,z,&east)) PROFILE_FAIL();
        if(d->class_id==2) {
            const int nx[4]={0,-1,0,1},nz[4]={-1,0,1,0};
            unsigned neighbors[4];unsigned k;neighbors[0]=north;neighbors[1]=west;neighbors[2]=south;neighbors[3]=east;
            ++s->wall_cells;
            for(k=0;k<4;++k) if(neighbors[k]!=2) {
                uint16_t nf;int32_t top_offset;int32_t ax=x0,az=z0,bx=x0,bz=z0,h,v;
                if(!object_at(r,DOK_WORLD_FCMAP,x+nx[k],z+nz[k],&nf) || !field(l,nf,0x54,&top_offset)) PROFILE_FAIL();
                h=word(height-top_offset+1);
                v=word(((int32_t)w->images[object].height*top_offset)/l->settings[DOK_LEVEL_CELL_HEIGHT]);
                if(h<=0) continue;
                if(k==0) bx+=width; /* orientation0 */
                else if(k==1) az+=width; /* orientation90 */
                else if(k==2) {ax+=width;az+=width;bz+=width;} /*180*/
                else {ax+=width;bx+=width;bz+=width;} /*270*/
                status=plane(w,c,s,object,ax,az,bx,bz,y,h,(int16_t)v,0);if(status) return status;
            }
        } else {
            int ns_open=(north!=2 && north!=4 && north!=7 && south!=2 && south!=4 && south!=7);
            int ew_open=(west!=2 && west!=4 && west!=7 && east!=2 && east!=4 && east!=7);
            ++s->center_cells;
            if(ns_open) {
                status=plane(w,c,s,object,x0,z0+width/2,x0+width,z0+width/2,y,height,0,width);if(status) return status;
            }
            if(ew_open) {
                status=plane(w,c,s,object,x0+width/2,z0,x0+width/2,z0+width,y,height,0,width);if(status) return status;
            }
        }
    }
    return DOK_RENDER_OK;
}
static void sort_faces(dok_renderer_workspace *w) {
    unsigned i;
    for(i=1;i<w->face_count;++i) {
        dok_renderer_face value=w->faces[i];unsigned j=i;
        while(j && w->faces[j-1].key<value.key) {w->faces[j]=w->faces[j-1];--j;}
        w->faces[j]=value;
    }
}
static void swap32(int32_t *a,int32_t *b) {int32_t n=*a;*a=*b;*b=n;}
static void raster(dok_renderer_workspace *w,const dok_renderer_face *original,const uint8_t *light,dok_renderer_stats *s) {
    dok_renderer_face f=*original;const dok_renderer_image *im=&w->images[f.image_index];
    int x,first,last,span,q0,q1;int wrote=0;
    if(f.x0>f.x1) {
        swap32(&f.x0,&f.x1);swap32(&f.top0,&f.top1);swap32(&f.bottom0,&f.bottom1);
        swap32(&f.depth0,&f.depth1);swap32(&f.u0,&f.u1);
    }
    span=(int)(f.x1-f.x0);first=clamp((int)f.x0,0,239);last=clamp((int)f.x1,0,239);
    if(span<=0 || f.x1<0 || f.x0>239) return;
    /* Bounded Q7 reciprocal approximation is a G05 raster choice. Projection,
     * face vertices and depth remain G04 exact. Constant-depth doors retain
     * exact affine U; complete original interpolation belongs to G11. */
    q0=(6553600/(f.depth0+200))>>8;q1=(6553600/(f.depth1+200))>>8;
    if(q0<1) q0=1;if(q1<1) q1=1;
    for(x=first;x<=last;++x) {
        int i=x-(int)f.x0,top=(int)(f.top0+(f.top1-f.top0)*i/span);
        int bottom=(int)(f.bottom0+(f.bottom1-f.bottom0)*i/span),begin,end,denom,u,vstart,vstep,y;
        int32_t numerator;
        uint8_t *destination;
        if(bottom<top || bottom<0 || top>=DOK_RENDER_SCENE_HEIGHT) continue;
        begin=clamp(top,0,DOK_RENDER_SCENE_HEIGHT-1);end=clamp(bottom,0,DOK_RENDER_SCENE_HEIGHT-1);
        denom=q0*(span-i)+q1*i;
        numerator=f.u0*q0*(span-i)+f.u1*q1*i;u=denom?numerator/denom:0;
        u=clamp(u,0,im->width-1);
        vstart=clamp(f.v0,0,im->height-1);
        vstep=bottom>top?((im->height-1-vstart)<<16)/(bottom-top):0;
        vstart=(vstart<<16)+(begin-top)*vstep;
        destination=w->frame+(unsigned)begin*240+(unsigned)x;
        for(y=begin;y<=end;++y,vstart+=vstep,destination+=240) {
            unsigned v=(unsigned)vstart>>16;uint8_t color=light[im->pixels[v*im->width+(unsigned)u]];
            if((im->flags&8u) && !color) {++s->transparent_pixels;continue;}
            *destination=color;++s->pixels_written;wrote=1;
        }
    }
    if(wrote) ++s->faces_drawn;
}
dok_renderer_status dok_renderer_draw(dok_renderer_workspace *w,const dok_runtime *r,const dok_rom_assets *a,
    unsigned level,unsigned language,const dok_projection_camera *c,dok_renderer_stats *stats) {
    dok_renderer_stats s;dok_renderer_status status;uint16_t id;dok_rom_resource light;unsigned i,x,y;
    if(!w || !r || !r->level || !a || !c || !stats) return DOK_RENDER_ARGUMENT;
    zero(&s,sizeof(s));s.face_capacity=DOK_RENDER_FACE_CAPACITY;
    if(level>=a->level_count || language>=a->language_count || !r->object_count || r->object_count>DOK_RUNTIME_MAX_OBJECTS ||
       r->object_count!=r->level->object_count || !r->level->definitions || !r->level->fields ||
       r->level->settings[DOK_LEVEL_WIDTH]!=64 || r->level->settings[DOK_LEVEL_LENGTH]!=64 || r->level->cells!=4096 ||
       r->level->settings[DOK_LEVEL_CELL_SIDE]!=480 || r->level->settings[DOK_LEVEL_CELL_HEIGHT]!=960 ||
       c->sine!=32768 || c->cosine || c->distance!=200 || c->scale!=8 || c->focal!=4096 || c->center_x!=159 || c->center_y!=79 ||
       c->x<0 || c->x>30720 || c->z<0 || c->z>30720 || c->y< -4096 || c->y>4096) {
        *stats=s;PROFILE_FAIL();
    }
    w->face_count=0;zero(w->image_known,sizeof(w->image_known));
    for(y=0;y<160;++y) for(x=0;x<240;++x) {
        w->frame[y*240+x]=y>=144?0:(uint8_t)r->level->settings[y<72?DOK_LEVEL_CEILING_COLOR:DOK_LEVEL_FLOOR_COLOR];
    }
    if(dok_rom_binding(&id,a,level,language,DOK_ROM_GLOBAL_OBJECT,DOK_ROM_SLOT_LIGHT)!=DOK_ROM_OK ||
       dok_rom_resource_at(&light,a,id,language)!=DOK_ROM_OK || light.size!=4096) {status=DOK_RENDER_RESOURCE;goto done;}
    status=build(w,r,a,level,language,c,&s);if(status) goto done;
    sort_faces(w);
    for(i=0;i<256;++i) w->light_row[i]=light.data[i];
    for(i=0;i<w->face_count;++i) raster(w,&w->faces[i],w->light_row,&s);
    status=DOK_RENDER_OK;
done:
    s.face_count=w->face_count;*stats=s;return status;
}
