#include "dok/renderer.h"
#include "dok/image.h"
#include <limits.h>
#include "renderer_trig.inc"
#if defined(_MSC_VER)
#define DOK_NOINLINE __declspec(noinline)
#else
#define DOK_NOINLINE __attribute__((noinline))
#endif
#ifdef DOK_RENDER_TRACE
#include <stdio.h>
#define PROFILE_FAIL() do {fprintf(stderr,"renderer profile line %d\n",__LINE__);return DOK_RENDER_PROFILE;} while(0)
#else
#define PROFILE_FAIL() return DOK_RENDER_PROFILE
#endif

#ifdef DOK_RENDERER_PROBE
extern void gba_render_stage(unsigned stage);
#define STAGE(n) gba_render_stage(n)
#else
#define STAGE(n) ((void)0)
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
    w->images[object].columns=0;w->images[object].scales=0;
#ifdef DOK_RENDERER_PROBE
    extern const uint8_t dok_render_bank[],dok_render_bank_end[];
    unsigned entry=((level*2+language)*(a->resource_count+1)+id)*8;
    const uint8_t *bp=dok_render_bank+entry;
    uint32_t off=(uint32_t)bp[0]|((uint32_t)bp[1]<<8)|((uint32_t)bp[2]<<16)|((uint32_t)bp[3]<<24);
    if(!off || off>(size_t)(dok_render_bank_end-dok_render_bank) ||
       (size_t)v.width*v.height>(size_t)(dok_render_bank_end-dok_render_bank)-off) return DOK_RENDER_RESOURCE;
    w->images[object].columns=dok_render_bank+off;
    uint32_t table=(uint32_t)bp[4]|((uint32_t)bp[5]<<8)|((uint32_t)bp[6]<<16)|((uint32_t)bp[7]<<24);
    if(table) {
        if((table&3) || table>(size_t)(dok_render_bank_end-dok_render_bank) ||
           v.height*4u>(size_t)(dok_render_bank_end-dok_render_bank)-table) return DOK_RENDER_RESOURCE;
        w->images[object].scales=(const uint32_t *)(const void *)(dok_render_bank+table);
    }
#endif
    w->image_known[object]=1;return DOK_RENDER_OK;
}
void dok_renderer_camera_heading(dok_projection_camera *c,int heading) {
    int n=heading%360;if(n<0) n+=360;
    c->sine=trig[n];c->cosine=trig[361+n];
}
dok_renderer_status dok_renderer_camera_from_runtime(dok_projection_camera *out,const dok_runtime *r) {
    dok_projection_camera c;const dok_runtime_actor *a;uint16_t index;
    if(!out || !r || !r->level || !r->player_index || r->player_index>r->object_count) return DOK_RENDER_ARGUMENT;
    index=(uint16_t)(r->player_index-1);a=dok_runtime_actor_at(r,index);
    if(!a || !dok_runtime_position(r,index,&c.x,&c.y,&c.z)) PROFILE_FAIL();
    dok_renderer_camera_heading(&c,a->heading);c.distance=200;c.scale=8;c.focal=4096;c.center_x=159;c.center_y=79;
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
#ifndef DOK_RENDER_REFERENCE
    /* Conservative horizontal frustum test, before division. With the fixed
     * native lens screenX=159+200*lateral/(depth+200). Two pixels of
     * margin cover the integer-factor/SAR rounding over this local envelope. */
    if((a.lateral*200 < -163*(a.depth+200) && b.lateral*200 < -163*(b.depth+200)) ||
       (a.lateral*200 > 163*(a.depth+200) && b.lateral*200 > 163*(b.depth+200))) {
        ++s->faces_rejected;return DOK_RENDER_OK;
    }
#endif
    if(!dok_projection_project(c,&a,&at) || !dok_projection_project(c,&b,&bt)) return DOK_RENDER_PROJECTION;
    a.vertical=add(a.vertical,height);b.vertical=add(b.vertical,height);
#ifdef DOK_RENDER_REFERENCE
    if(!dok_projection_project(c,&a,&ab) || !dok_projection_project(c,&b,&bb)) return DOK_RENDER_PROJECTION;
#else
    /* Bottom shares exactly the top's denominator and quotient. */
    ab.y=add(sar(mul(a.vertical,at.factor),15),c->center_y);
    bb.y=add(sar(mul(b.vertical,bt.factor),15),c->center_y);
#endif
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
    /* Rotated four-corner G05 visibility envelope. Port policy, not yet
     * a reconstruction of every mixed-width original AABB instruction. */
    int xmin=side-1,xmax=0,zmin=side-1,zmax=0,x,z;
    const int lateral[4]={-1,1,-6,6},depth[4]={-2,-2,7,7};
    for(unsigned k=0;k<4;++k) {
        int dx=sar((lateral[k]*c->cosine-depth[k]*c->sine)*cell,15);
        int dz=sar((lateral[k]*c->sine+depth[k]*c->cosine)*cell,15);
        int cx=clamp((c->x+dx)/cell,0,side-1),cz=clamp((c->z+dz)/cell,0,side-1);
        if(cx<xmin) xmin=cx;if(cx>xmax) xmax=cx;
        if(cz<zmin) zmin=cz;if(cz>zmax) zmax=cz;
    }
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
#ifndef DOK_RENDER_REFERENCE
        {
            dok_camera_point mid;int32_t sa=c->sine<0?-c->sine:c->sine,ca=c->cosine<0?-c->cosine:c->cosine;
            int32_t radius=(width*(sa+ca)+65535)/65536+2;
            dok_projection_transform(c,x0+width/2,y,z0+width/2,&mid);
            int32_t far=mid.depth+radius;
            if(far<=0 || (mid.lateral-radius)*200>163*(far+200) ||
               (mid.lateral+radius)*200< -163*(far+200)) continue;
        }
#endif
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
    uint16_t order[DOK_RENDER_FACE_CAPACITY];unsigned i;
    for(i=0;i<w->face_count;++i) {
        unsigned j=i;while(j && w->faces[order[j-1]].key<w->faces[i].key) {order[j]=order[j-1];--j;}
        order[j]=(uint16_t)i;
    }
    /* Apply stable permutation in cycles: move each 48-byte face once. */
    for(i=0;i<w->face_count;++i) if(order[i]!=i) {
        dok_renderer_face saved=w->faces[i];unsigned j=i;
        while(order[j]!=i) {unsigned next=order[j];w->faces[j]=w->faces[next];order[j]=(uint16_t)j;j=next;}
        w->faces[j]=saved;order[j]=(uint16_t)j;
    }
}
static void swap32(int32_t *a,int32_t *b) {int32_t n=*a;*a=*b;*b=n;}
#ifndef DOK_RENDER_REFERENCE
typedef struct {unsigned quotient,remainder,step,carry,divisor;int sign;} edge_walk;
static edge_walk edge_start(int delta,unsigned offset,unsigned span) {
    edge_walk e;unsigned magnitude=(unsigned)(delta<0?-delta:delta),n=magnitude*offset;
    e.quotient=n/span;e.remainder=n%span;e.step=magnitude/span;e.carry=magnitude%span;
    e.divisor=span;e.sign=delta<0?-1:1;return e;
}
static void edge_next(edge_walk *e) {
    e->quotient+=e->step;e->remainder+=e->carry;
    if(e->remainder>=e->divisor) {e->remainder-=e->divisor;++e->quotient;}
}
#endif
#ifndef DOK_RENDERER_PROBE
static DOK_NOINLINE unsigned column(uint8_t *restrict destination,const uint8_t *restrict pixels,
    const uint8_t *restrict light,unsigned width,unsigned v,unsigned step,unsigned count,unsigned masked) {
    unsigned written=0;
    if(masked) {
        for(unsigned n=0;n<count;++n,v+=step,destination+=240) {
            uint8_t color=light[pixels[(v>>16)*width]];
            if(color) {*destination=color;++written;}
        }
    } else {
        for(unsigned n=0;n<count;++n,v+=step,destination+=240)
            *destination=light[pixels[(v>>16)*width]];
        written=count;
    }
    return written;
}
#endif
static DOK_NOINLINE unsigned mapped_column(uint8_t *restrict destination,const uint8_t *restrict pixels,
    unsigned v,unsigned step,unsigned count,unsigned masked) {
    unsigned written=0;
    if(masked) {
        for(unsigned n=0;n<count;++n,v+=step,destination+=240) {
            uint8_t color=pixels[v>>16];if(color) {*destination=color;++written;}
        }
    } else {
        unsigned n=0;
        /* Four independent stores amortize the loop branch without changing
         * any Q16 sample, including repeated texels and clipped starts. */
        for(;n+4<=count;n+=4,destination+=960) {
            destination[0]=pixels[v>>16];v+=step;
            destination[240]=pixels[v>>16];v+=step;
            destination[480]=pixels[v>>16];v+=step;
            destination[720]=pixels[v>>16];v+=step;
        }
        for(;n<count;++n,v+=step,destination+=240) *destination=pixels[v>>16];
        written=count;
    }
    return written;
}
#ifdef DOK_RENDERER_PROBE
static DOK_NOINLINE unsigned strip_column(uint8_t *restrict dst,const uint8_t *restrict src,unsigned count) {
    unsigned n=count;
    while(n && ((uintptr_t)src&3)) {*dst=*src++;dst+=240;--n;}
    while(n>=4) {
        uint32_t four=*(const uint32_t *)(const void *)src;src+=4;
        dst[0]=(uint8_t)four;dst[240]=(uint8_t)(four>>8);
        dst[480]=(uint8_t)(four>>16);dst[720]=(uint8_t)(four>>24);
        dst+=960;n-=4;
    }
    while(n--) {*dst=*src++;dst+=240;}
    return count;
}
static unsigned draw_run(uint8_t *dst,const uint8_t *pixels,unsigned v,unsigned step,unsigned count,const uint8_t *strip) {
    return strip?strip_column(dst,strip,count):mapped_column(dst,pixels,v,step,count,0);
}
static void cover(dok_renderer_workspace *w,unsigned x,unsigned lo,unsigned hi) {
    unsigned count=w->covered_count[x],a=0,b;
    while(a<count && (unsigned)w->covered[x][a][1]+1<lo) ++a;
    b=a;
    while(b<count && w->covered[x][b][0]<=hi+1) {
        if(w->covered[x][b][0]<lo) lo=w->covered[x][b][0];
        if(w->covered[x][b][1]>hi) hi=w->covered[x][b][1];++b;
    }
    if(b==a) {
        /* At most72 non-adjacent runs fit in144 rows. A new disjoint run
         * cannot exist once count72 is reached. */
        for(unsigned n=count;n>a;--n) {w->covered[x][n][0]=w->covered[x][n-1][0];w->covered[x][n][1]=w->covered[x][n-1][1];}
        ++count;
    } else {
        for(unsigned n=b;n<count;++n) {w->covered[x][a+1+n-b][0]=w->covered[x][n][0];w->covered[x][a+1+n-b][1]=w->covered[x][n][1];}
        count-=b-a-1;
    }
    w->covered[x][a][0]=(uint8_t)lo;w->covered[x][a][1]=(uint8_t)hi;w->covered_count[x]=(uint8_t)count;
}
static DOK_NOINLINE unsigned visible_column(dok_renderer_workspace *w,unsigned x,unsigned begin,unsigned end,
    const uint8_t *pixels,unsigned v,unsigned step,unsigned masked,const uint8_t *record,unsigned strip_first) {
    uint8_t gaps[73][2];unsigned n=0,start=begin,written=0,available=0,count=w->covered_count[x];
    const uint8_t *strip=record?record+record[1]+strip_first:0;
    /* The ordinary wall case has one continuous covered interval. Avoid
     * materializing a gap list and calling the general interval merger. */
    if(!masked) {
        if(!count) {
            draw_run(w->frame+begin*240+x,pixels,v,step,end-begin+1,strip);
            w->covered[x][0][0]=(uint8_t)begin;w->covered[x][0][1]=(uint8_t)end;
            w->covered_count[x]=1;return end-begin+1;
        }
        unsigned lo=w->covered[x][0][0],hi=w->covered[x][0][1];
        if(count==1 && begin<=hi+1 && end+1>=lo) {
            if(begin<lo) {
                unsigned last=end<lo?end:lo-1;
                written+=draw_run(w->frame+begin*240+x,pixels,v,step,last-begin+1,strip);
                w->covered[x][0][0]=(uint8_t)begin;
            }
            if(end>hi) {
                unsigned first=begin>hi?begin:hi+1;
                written+=draw_run(w->frame+first*240+x,pixels,v+(first-begin)*step,step,end-first+1,strip?strip+(first-begin):0);
                w->covered[x][0][1]=(uint8_t)end;
            }
            return written;
        }
    }
    for(unsigned k=0;k<count && start<=end;++k) {
        unsigned lo=w->covered[x][k][0],hi=w->covered[x][k][1];
        if(hi<start) continue;if(lo>end) break;
        if(lo>start) {gaps[n][0]=(uint8_t)start;gaps[n++][1]=(uint8_t)(lo-1);}
        start=hi+1;
    }
    if(start<=end) {gaps[n][0]=(uint8_t)start;gaps[n++][1]=(uint8_t)end;}
    for(unsigned k=0;k<n;++k) {
        unsigned lo=gaps[k][0],hi=gaps[k][1],pos=v+(lo-begin)*step;available+=hi-lo+1;
        uint8_t *dst=w->frame+lo*240+x;
        if(!masked) {
            written+=draw_run(dst,pixels,pos,step,hi-lo+1,strip?strip+(lo-begin):0);cover(w,x,lo,hi);
        } else if(record) {
            for(unsigned j=0;j<record[0];++j) {
                int first=(int)begin+(int)record[4+j*2]-(int)strip_first;
                int last=(int)begin+(int)record[5+j*2]-(int)strip_first;
                if(last<(int)lo) continue;if(first>(int)hi) break;
                if(first<(int)lo) first=(int)lo;if(last>(int)hi) last=(int)hi;
                written+=strip_column(w->frame+(unsigned)first*240+x,strip+((unsigned)first-begin),(unsigned)(last-first+1));
                cover(w,x,(unsigned)first,(unsigned)last);
            }
        } else {
            unsigned run=144;
            for(unsigned y=lo;y<=hi;++y,pos+=step,dst+=240) {
                uint8_t color=pixels[pos>>16];
                if(color) {*dst=color;++written;if(run==144) run=y;}
                else if(run!=144) {cover(w,x,run,y-1);run=144;}
            }
            if(run!=144) cover(w,x,run,hi);
        }
    }
    return written|((available-written)<<16);
}
#endif
static void raster(dok_renderer_workspace *w,const dok_renderer_face *original,const uint8_t *light,dok_renderer_stats *s) {
#ifdef DOK_RENDERER_PROBE
    (void)light;
#endif
    dok_renderer_face f=*original;const dok_renderer_image *im=&w->images[f.image_index];
    int x,first,last,span,q0,q1;int wrote=0;unsigned pixels=0,transparent=0;
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
#ifndef DOK_RENDER_REFERENCE
    /* i is nonnegative after clipping X. Accumulate abs(delta)*i/span
     * with quotient/remainder, then restore the sign for exact trunc0. */
    edge_walk upper=edge_start(f.top1-f.top0,(unsigned)(first-f.x0),(unsigned)span);
    edge_walk lower=edge_start(f.bottom1-f.bottom0,(unsigned)(first-f.x0),(unsigned)span);
#endif
    for(x=first;x<=last;++x) {
#ifdef DOK_RENDER_REFERENCE
        int i=x-(int)f.x0,top=(int)(f.top0+(f.top1-f.top0)*i/span);
        int bottom=(int)(f.bottom0+(f.bottom1-f.bottom0)*i/span);
#else
        int i=x-(int)f.x0,top=f.top0+upper.sign*(int)upper.quotient;
        int bottom=f.bottom0+lower.sign*(int)lower.quotient;
        edge_next(&upper);edge_next(&lower);
#endif
        int begin,end,denom,u,vstart,vstep,y;
        int32_t numerator;
#ifndef DOK_RENDERER_PROBE
        uint8_t *destination;
#endif
        if(bottom<top || bottom<0 || top>=DOK_RENDER_SCENE_HEIGHT) continue;
        begin=clamp(top,0,DOK_RENDER_SCENE_HEIGHT-1);end=clamp(bottom,0,DOK_RENDER_SCENE_HEIGHT-1);
#ifdef DOK_RENDERER_PROBE
        unsigned occluded=0;
        for(unsigned k=0;k<w->covered_count[x];++k)
            if(w->covered[x][k][0]<=begin && w->covered[x][k][1]>=end) {occluded=1;break;}
        if(occluded) continue;
#endif
        denom=q0*(span-i)+q1*i;
        numerator=f.u0*q0*(span-i)+f.u1*q1*i;u=denom?numerator/denom:0;
        u=clamp(u,0,im->width-1);
        vstart=clamp(f.v0,0,im->height-1);
#ifdef DOK_RENDERER_PROBE
        const uint8_t *strip=0;
        if(im->scales && im->scales[vstart] && bottom>top && bottom-top<=128) {
            extern const uint8_t dok_render_bank[];
            const uint32_t *spans=(const uint32_t *)(const void *)(dok_render_bank+im->scales[vstart]);
            const uint32_t *columns=(const uint32_t *)(const void *)(dok_render_bank+spans[bottom-top]);
            strip=dok_render_bank+columns[u];
        }
        if(strip) {vstart=0;vstep=0;} else
#endif
        {
        vstep=bottom>top?((im->height-1-vstart)<<16)/(bottom-top):0;
        vstart=(vstart<<16)+(begin-top)*vstep;
        }
#ifndef DOK_RENDERER_PROBE
        destination=w->frame+(unsigned)begin*240+(unsigned)x;
#endif
        y=end-begin+1;(void)y;
        unsigned n;
#ifdef DOK_RENDERER_PROBE
        n=visible_column(w,(unsigned)x,(unsigned)begin,(unsigned)end,im->columns+(unsigned)u*im->height,
            (unsigned)vstart,(unsigned)vstep,im->flags&8u,strip,(unsigned)(begin-top));
#else
        n=im->columns?
            mapped_column(destination,im->columns+(unsigned)u*im->height,(unsigned)vstart,(unsigned)vstep,(unsigned)y,im->flags&8u):
            column(destination,im->pixels+u,light,im->width,(unsigned)vstart,(unsigned)vstep,(unsigned)y,im->flags&8u);
#endif
#ifdef DOK_RENDERER_PROBE
        transparent+=n>>16;n&=65535u;
#else
        transparent+=(unsigned)y-n;
#endif
        pixels+=n;if(n) wrote=1;
    }
    s->pixels_written+=pixels;s->transparent_pixels+=transparent;
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
       c->sine< -32768 || c->sine>32768 || c->cosine< -32768 || c->cosine>32768 || c->distance!=200 || c->scale!=8 || c->focal!=4096 || c->center_x!=159 || c->center_y!=79 ||
       c->x<0 || c->x>30720 || c->z<0 || c->z>30720 || c->y< -4096 || c->y>4096) {
        *stats=s;PROFILE_FAIL();
    }
    STAGE(0);w->face_count=0;zero(w->image_known,sizeof(w->image_known));
    for(y=0;y<160;++y) {
        uint32_t color=y>=144?0:(uint8_t)r->level->settings[y<72?DOK_LEVEL_CEILING_COLOR:DOK_LEVEL_FLOOR_COLOR];
        uint32_t *row=w->frame_words+y*60;
        color*=0x01010101u;
        for(x=0;x<60;x+=4) {row[x]=color;row[x+1]=color;row[x+2]=color;row[x+3]=color;}
    }
    if(dok_rom_binding(&id,a,level,language,DOK_ROM_GLOBAL_OBJECT,DOK_ROM_SLOT_LIGHT)!=DOK_ROM_OK ||
       dok_rom_resource_at(&light,a,id,language)!=DOK_ROM_OK || light.size!=4096) {status=DOK_RENDER_RESOURCE;goto done;}
    STAGE(1);status=build(w,r,a,level,language,c,&s);if(status) goto done;
    STAGE(2);sort_faces(w);STAGE(3);
    for(i=0;i<256;++i) w->light_row[i]=light.data[i];
#ifdef DOK_RENDERER_PROBE
    zero(w->covered_count,sizeof(w->covered_count));
    for(i=w->face_count;i>0;--i) raster(w,&w->faces[i-1],w->light_row,&s);
#else
    for(i=0;i<w->face_count;++i) raster(w,&w->faces[i],w->light_row,&s);
#endif
    STAGE(4);status=DOK_RENDER_OK;
done:
    s.face_count=w->face_count;*stats=s;return status;
}
