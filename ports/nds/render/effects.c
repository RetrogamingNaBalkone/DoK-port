#include "dok/effects.h"
#include "dok/sha256.h"
#include <limits.h>
#include <string.h>
#include <stdlib.h>

static int32_t word(int64_t n)
{ uint32_t u=(uint32_t)(uint64_t)n&65535u;return u<32768u?(int32_t)u:(int32_t)u-65536; }
static int32_t wrap(int64_t n)
{ uint32_t u=(uint32_t)(uint64_t)n;return u<=INT32_MAX?(int32_t)u:-1-(int32_t)(UINT32_MAX-u); }
static int64_t floor_div(int64_t n,int64_t d)
{ int64_t q=n/d;if(n%d && ((n<0)!=(d<0)))--q;return q; }
uint32_t dok_light_row(int32_t depth_q16,int16_t near_depth)
{
    int32_t d=word(floor_div(depth_q16,65536));
    int32_t delta=d<near_depth?0:word(d-near_depth);
    uint32_t row=(uint32_t)word(delta/120)&65535u;
    return row<16u?row:15u;
}
dok_status dok_light_palette(const unsigned char rgb6[768],const unsigned char light[4096],
    uint32_t percentage,uint16_t out[4096])
{
    uint32_t i;
    if(!rgb6 || !light || !out || percentage>100u)return DOK_STATUS_INVALID_ARGUMENT;
    for(i=0;i<768u;++i)if(rgb6[i]>63u)return DOK_STATUS_INVALID_ARGUMENT;
    for(i=0;i<4096u;++i) {
        const unsigned char *p=rgb6+(uint32_t)light[i]*3u;
        uint32_t r=(p[0]*percentage/100u)>>1,g=(p[1]*percentage/100u)>>1,b=(p[2]*percentage/100u)>>1;
        out[i]=(uint16_t)(r|(g<<5)|(b<<10));
    }
    return DOK_STATUS_OK;
}
dok_status dok_effects_palette_load(const dok_asset_session *session,const dok_asset_reader *reader,
    dok_asset_id palette,unsigned char candidate[768],unsigned char out[768])
{
    dok_asset_entry e;uint32_t actual=0,i;dok_sha256 state;unsigned char hash[32];
    dok_asset_id raw=(palette&~UINT32_C(0x0003fc00))|((uint32_t)DOK_ASSET_RAW_SOURCE<<10);
    if(!session || !reader || !reader->read_at || !candidate || !out || candidate==out ||
       dok_assets_find(session,raw,&e)!=DOK_ASSET_OK || e.source_kind!=4u || e.bytes!=768u ||
       e.offset>reader->file_bytes || e.bytes>reader->file_bytes-e.offset)return DOK_STATUS_INVALID_ARGUMENT;
    if(reader->read_at(reader->context,e.offset,candidate,768,&actual)!=DOK_ASSET_OK || actual!=768u)
        return DOK_STATUS_INVALID_STATE;
    dok_sha256_begin(&state);dok_sha256_update(&state,candidate,768);dok_sha256_end(&state,hash);
    if(memcmp(hash,e.sha256,32))return DOK_STATUS_INVALID_STATE;
    for(i=0;i<768u;++i)if(candidate[i]>63u)return DOK_STATUS_INVALID_STATE;
    memcpy(out,candidate,768);return DOK_STATUS_OK;
}
dok_status dok_effects_pixels(const unsigned char *source,uint16_t width,uint16_t height,
    uint16_t sw,uint16_t sh,uint16_t flags,uint16_t divisor,uint16_t phase,
    const unsigned char *light_row,uint32_t masked,unsigned char *out,uint32_t capacity)
{
    uint32_t x,y,axis=0,cyclic=0,shift=0;int32_t direction=0;
    const uint16_t masks[4]={0x0440u,0x0880u,0x1100u,0x2200u};
    if(!source || !out || source==out || !width || !height || width>sw || height>sh ||
       (uint32_t)sw*sh>capacity || masked>1u || (masked && (!light_row || light_row[0])) || (flags&0x4000u))
        return DOK_STATUS_INVALID_ARGUMENT;
    if(!divisor)divisor=1;
    if(phase>divisor)phase=divisor;
    for(x=0;x<4u;++x)if(flags&masks[x]) {
        axis=x/2u;direction=(x&1u)?1:-1;cyclic=(flags&(masks[x]&0x3c00u))!=0;
        shift=((axis?height:width)/divisor)*phase;break;
    }
    memset(out,0,(uint32_t)sw*sh);
    /* A source crop is a row translation, not a general resampler. Copy at
     * most two spans per row; keep identical division-before-multiply and
     * zero padding. Avoid four ARM software modulo operations per texel. */
    int32_t dx=direction && !axis?direction*(int32_t)shift:0;
    int32_t dy=direction && axis?direction*(int32_t)shift:0;
    if(cyclic){dx=(dx%(int32_t)width+(int32_t)width)%(int32_t)width;dy=(dy%(int32_t)height+(int32_t)height)%(int32_t)height;}
    for(y=0;y<height;++y) {
        int32_t sy=(int32_t)y+dy;
        if(cyclic && sy>=height)sy-=height;
        if(sy<0 || sy>=height)continue;
        const unsigned char *src=source+(uint32_t)sy*sw;
        unsigned char *dst=out+y*sw;
        if(cyclic) {
            uint32_t first=width-(uint32_t)dx;
            memcpy(dst,src+dx,first);
            if(dx)memcpy(dst+first,src,(uint32_t)dx);
        } else {
            uint32_t left=dx<0?(uint32_t)-dx:0u;
            uint32_t right=dx>0?(uint32_t)dx:0u;
            if(left+right<width)memcpy(dst+left,src+right,width-left-right);
        }
        if(masked)for(x=0;x<width;++x)if(!light_row[dst[x]])dst[x]=0;
    }
    return DOK_STATUS_OK;
}
dok_status dok_water_step(dok_water_state *s,uint32_t *cancel)
{
    if(!s || !cancel || s->counter<0 || s->counter>18 || s->revision==UINT32_MAX)
        return DOK_STATUS_INVALID_ARGUMENT;
    if(s->counter==18) { *cancel=1;return DOK_STATUS_OK; }
    s->displayed_phase=s->counter++;++s->revision;*cancel=0;return DOK_STATUS_OK;
}
dok_status dok_water_rearm(dok_water_state *s,uint32_t *arm)
{
    if(!s || !arm || s->counter<0 || s->counter>18)return DOK_STATUS_INVALID_ARGUMENT;
    *arm=s->counter==18;if(*arm)s->counter=0;return DOK_STATUS_OK;
}

typedef struct Curve {
    int32_t horizontal,d0,d1,a0,a1;
    int64_t cuts[15];uint32_t count;
} Curve;
typedef struct Poly { dok_nds_render_vertex v[12];uint32_t count; } Poly;
static int64_t depth(const dok_nds_render_camera *c,const dok_light_origin *o,const dok_nds_render_vertex *v)
{ return ((int64_t)o->z+v->z-c->z)*c->cos_q15-((int64_t)o->x+v->x-c->x)*c->sin_q15; }
static int64_t plane(const dok_nds_render_camera *c,const dok_light_origin *o,
    const dok_nds_render_vertex *v,uint32_t axis,int64_t cut)
{
    int64_t d=depth(c,o,v);
    int64_t lateral=axis?((int64_t)o->y+v->y-c->y)*32768:
        ((int64_t)o->x+v->x-c->x)*c->cos_q15+((int64_t)o->z+v->z-c->z)*c->sin_q15;
    return ((axis?79:159)*INT64_C(65536)-cut)*(d+INT64_C(200)*32768)+INT64_C(200)*65536*lateral;
}
static int32_t coordinate(const dok_nds_render_vertex *v,uint32_t n)
{ return n==0?v->x:n==1?v->y:n==2?v->z:n==3?v->u_q16:v->v_q16; }
static dok_nds_render_vertex intersect(const dok_nds_render_vertex *a,const dok_nds_render_vertex *b,int64_t da,int64_t db)
{
    int32_t values[5];uint32_t k;
    while(da>INT64_C(4294967295) || da < -INT64_C(4294967295) ||
          db>INT64_C(4294967295) || db < -INT64_C(4294967295)) { da/=2;db/=2; }
    for(k=0;k<5u;++k)values[k]=(int32_t)floor_div((int64_t)coordinate(a,k)*db-(int64_t)coordinate(b,k)*da,db-da);
    return (dok_nds_render_vertex){values[0],values[1],values[2],values[3],values[4]};
}
static int clip(Poly *p,const dok_nds_render_camera *c,const dok_light_origin *o,uint32_t axis,int64_t cut,int lower,int near_clip)
{
    Poly result={0};uint32_t i;
    for(i=0;i<p->count;++i) {
        const dok_nds_render_vertex *a=&p->v[i],*b=&p->v[(i+1u)%p->count];
        int64_t da=near_clip?depth(c,o,a):plane(c,o,a,axis,cut);
        int64_t db=near_clip?depth(c,o,b):plane(c,o,b,axis,cut);
        if(!lower) {da=-da;db=-db;}
        if(da>=0) {if(result.count==12u)return 0;result.v[result.count++]=*a;}
        if((da>=0)!=(db>=0)) {if(result.count==12u)return 0;result.v[result.count++]=intersect(a,b,da,db);}
    }
    *p=result;return 1;
}
static int projected(const dok_nds_render_camera *c,const dok_light_origin *o,
    const dok_nds_render_vertex *v,uint32_t axis,int32_t *d,int32_t *a)
{
    int32_t z=(int32_t)floor_div(wrap(depth(c,o,v)),32768);
    int32_t x=axis?(o->y+v->y-c->y):
        (int32_t)floor_div(wrap(((int64_t)o->x+v->x-c->x)*c->cos_q15+((int64_t)o->z+v->z-c->z)*c->sin_q15),32768);
    /* Source horizontal constructor uses SAR20 for negative depth. */
    if(axis && z<0)z=(int32_t)floor_div(z,32);
    if(z<=-200)z=-199; /* Outside the native valid denominator: explicit DS near adaptation. */
    int32_t f=6553600/(z+200);
    int32_t value=(int32_t)floor_div(wrap((int64_t)x*f),32768)+(axis?79:159);
    if(value < -16384)value=-16384;
    if(value>16384)value=16384;
    *d=z<0?-z:z;*a=value;return 1;
}
static int curve(const dok_nds_render_quad *q,const dok_nds_render_camera *c,const dok_light_origin *o,int16_t near_depth,Curve *out)
{
    Curve r={0};uint32_t k;
    r.horizontal=q->vertex[0].y==q->vertex[1].y && q->vertex[0].y==q->vertex[2].y && q->vertex[0].y==q->vertex[3].y;
    for(k=0;k<4u;++k) {
        int32_t d,a;projected(c,o,&q->vertex[k],(uint32_t)r.horizontal,&d,&a);
        if(!k || a<r.a0) {r.a0=a;r.d0=d;}
        if(!k || a>r.a1) {r.a1=a;r.d1=d;}
    }
    if(r.a0!=r.a1 && r.d0!=r.d1)for(k=1;k<16u;++k) {
        int64_t cut=(int64_t)r.a0*65536+((int64_t)near_depth+120*(int64_t)k-r.d0)*
            (r.a1-r.a0)*65536/(r.d1-r.d0);
        if(cut>(int64_t)r.a0*65536 && cut<(int64_t)r.a1*65536) {
            uint32_t index=r.count++;
            while(index && r.cuts[index-1u]>cut) {r.cuts[index]=r.cuts[index-1u];--index;}
            r.cuts[index]=cut;
        }
    }
    *out=r;return 1;
}
static uint32_t row_at(const Curve *r,const dok_nds_render_camera *c,const dok_light_origin *o,const Poly *p,int16_t near_depth)
{
    uint32_t k;int64_t sx=0,sy=0,sz=0;
    for(k=0;k<p->count;++k) {sx+=p->v[k].x;sy+=p->v[k].y;sz+=p->v[k].z;}
    dok_nds_render_vertex v={(int32_t)(sx/p->count),(int32_t)(sy/p->count),(int32_t)(sz/p->count),0,0};
    int64_t d=depth(c,o,&v),eye=d+INT64_C(200)*32768;
    int64_t numerator=r->horizontal?((int64_t)o->y+v.y-c->y)*32768:
        ((int64_t)o->x+v.x-c->x)*c->cos_q15+((int64_t)o->z+v.z-c->z)*c->sin_q15;
    int64_t axis=(r->horizontal?79:159)*INT64_C(65536)+INT64_C(200)*65536*numerator/eye;
    int64_t interpolated=(int64_t)r->d0*65536;
    if(r->a0!=r->a1)interpolated+=(axis-(int64_t)r->a0*65536)*(r->d1-r->d0)/(r->a1-r->a0);
    return dok_light_row(wrap(interpolated),near_depth);
}
/* The accepted N07 conservative frustum, applied again after subdivision.
 * It retains a margin around the actual N06 GPU viewport. */
static int piece_visible(const dok_nds_render_quad *q,const dok_nds_render_camera *c,const dok_light_origin *o)
{
    uint32_t common=31u;
    for(uint32_t k=0;k<4u;++k) {
        int64_t dx=(int64_t)o->x+q->vertex[k].x-c->x,dz=(int64_t)o->z+q->vertex[k].z-c->z;
        int64_t lateral=dx*c->cos_q15+dz*c->sin_q15,d=dz*c->cos_q15-dx*c->sin_q15;
        int64_t eye=d+INT64_C(200)*32768,dy=((int64_t)o->y+q->vertex[k].y-c->y)*32768;
        common&=(d+32768<0?1u:0u)|(400*lateral+324*eye<0?2u:0u)|(326*eye-400*lateral<0?4u:0u)|
            (164*eye+400*dy<0?8u:0u)|(166*eye-400*dy<0?16u:0u);
        if(!common)return 1;
    }
    return 0;
}
static uint32_t pieces(const dok_nds_render_quad *q,const dok_nds_render_camera *c,const dok_light_origin *o,
    int16_t near_depth,dok_nds_render_quad out[DOK_LIGHT_PIECES])
{
    Curve r;Poly base={0};uint32_t k,n=0;
    curve(q,c,o,near_depth,&r);base.count=4;
    for(k=0;k<4u;++k)base.v[k]=q->vertex[k];
    if(!clip(&base,c,o,0,0,1,1))return UINT32_MAX;
    if(base.count<3u)return 0;
    for(k=0;k<=r.count;++k) {
        Poly p=base;uint32_t first,row;
        /* Partition the remaining polygon once at each boundary. Both sides
         * receive the same quantized intersection; independently re-clipping
         * the source at two cuts creates one-unit cracks after rounding. */
        if(k<r.count) {
            if(!clip(&p,c,o,(uint32_t)r.horizontal,r.cuts[k],0,0) ||
               !clip(&base,c,o,(uint32_t)r.horizontal,r.cuts[k],1,0))return UINT32_MAX;
        }
        if(p.count<3u)continue;
        row=row_at(&r,c,o,&p,near_depth);
        for(first=1;first+1u<p.count;first+=2u) {
            if(n==DOK_LIGHT_PIECES)return UINT32_MAX;
            out[n]=*q;out[n].reserved=(uint16_t)row;
            out[n].vertex[0]=p.v[0];out[n].vertex[1]=p.v[first];out[n].vertex[2]=p.v[first+1u];
            out[n].vertex[3]=p.v[first+2u<p.count?first+2u:first+1u];
            if(piece_visible(&out[n],c,o))++n;
        }
    }
    return n;
}

/* DS raster edge slopes are quantized. A T junction between independently
 * split wall/floor edges can expose pixels even if XYZ is collinear. Both
 * owners therefore receive the identical ordered edge partition. */
static int key_compare(const void *aa,const void *bb)
{uint64_t a=*(const uint64_t *)aa,b=*(const uint64_t *)bb;return a<b?-1:a>b;}
static int edge_key(const dok_light_origin *o,const dok_nds_render_vertex *a,
    const dok_nds_render_vertex *b,uint64_t *ka,uint64_t *kb)
{
    int32_t av[3]={o->x+a->x,o->y+a->y,o->z+a->z},bv[3]={o->x+b->x,o->y+b->y,o->z+b->z};
    uint32_t axis=3,changes=0,k;uint64_t prefix;
    for(k=0;k<3u;++k)if(av[k]!=bv[k]){axis=k;++changes;}
    if(changes!=1u)return 0;
    for(k=0;k<3u;++k)if(av[k]<-8192 || av[k]>49151 || bv[k]<-8192 || bv[k]>49151)return -1;
    prefix=((uint64_t)axis<<48)|((uint64_t)(av[(axis+1u)%3u]+8192)<<32)|((uint64_t)(av[(axis+2u)%3u]+8192)<<16);
    *ka=prefix|(uint32_t)(av[axis]+8192);*kb=prefix|(uint32_t)(bv[axis]+8192);return 1;
}
static uint32_t edge_lower(const uint64_t *keys,uint32_t count,uint64_t key)
{uint32_t l=0;while(l<count){uint32_t m=l+(count-l)/2u;if(keys[m]<key)l=m+1u;else count=m;}return l;}
static void orient_quad(dok_nds_render_quad *q)
{
    for(uint32_t k=0;k<4u;++k) {
        dok_nds_render_vertex a=q->vertex[0],b=q->vertex[1],c=q->vertex[2];
        int64_t ux=b.x-a.x,uy=b.y-a.y,uz=b.z-a.z,vx=c.x-a.x,vy=c.y-a.y,vz=c.z-a.z;
        if(uy*vz-uz*vy || uz*vx-ux*vz || ux*vy-uy*vx)return;
        q->vertex[0]=q->vertex[1];q->vertex[1]=q->vertex[2];q->vertex[2]=q->vertex[3];q->vertex[3]=a;
    }
}
static uint32_t weld_piece(const dok_nds_render_quad *q,const dok_light_origin *o,
    const uint64_t *keys,uint32_t count,dok_nds_render_quad out[DOK_LIGHT_PIECES])
{
    dok_nds_render_vertex boundary[DOK_LIGHT_PIECES*2u];uint32_t n=0,k,pieces_count=0;
    for(k=0;k<4u;++k) {
        const dok_nds_render_vertex *a=&q->vertex[k],*b=&q->vertex[(k+1u)%4u];uint64_t ka,kb;
        if(n==DOK_LIGHT_PIECES*2u)return UINT32_MAX;
        boundary[n++]=*a;
        int result=edge_key(o,a,b,&ka,&kb);if(result<0)return UINT32_MAX;if(!result)continue;
        uint64_t lo=ka<kb?ka:kb,hi=ka<kb?kb:ka;uint32_t begin=edge_lower(keys,count,lo+1u),end=edge_lower(keys,count,hi);
        for(uint32_t j=begin;j<end;++j) {
            uint64_t key=keys[ka<kb?j:end-1u-(j-begin)];
            int32_t t=(int32_t)(key&65535u)-(int32_t)(ka&65535u),d=(int32_t)(kb&65535u)-(int32_t)(ka&65535u),v[5];
            if(n==DOK_LIGHT_PIECES*2u)return UINT32_MAX;
            for(uint32_t f=0;f<5u;++f)v[f]=coordinate(a,f)+(int32_t)((int64_t)(coordinate(b,f)-coordinate(a,f))*t/d);
            boundary[n++]=(dok_nds_render_vertex){v[0],v[1],v[2],v[3],v[4]};
        }
    }
    if(n==4u){out[0]=*q;return 1;}
    for(k=1;k+1u<n;k+=2u) {
        if(pieces_count==DOK_LIGHT_PIECES)return UINT32_MAX;
        dok_nds_render_quad *p=&out[pieces_count++];*p=*q;
        p->vertex[0]=boundary[0];p->vertex[1]=boundary[k];p->vertex[2]=boundary[k+1u];p->vertex[3]=boundary[k+2u<n?k+2u:k+1u];
        orient_quad(p);
    }
    return pieces_count;
}
static dok_render_scene_result weld(dok_render_scene *s,dok_light_workspace *w)
{
    uint32_t i,j,count=0,source=0,total=0,dest;
    for(i=0;i<s->frame.cell_count;++i) {
        const dok_nds_render_cell *cell=&s->cells[i];dok_light_origin o={cell->origin_x,cell->origin_y,cell->origin_z,0};
        for(j=0;j<cell->quad_count;++j) {
            const dok_nds_render_quad *q=&cell->quads[j];w->origins[source++]=o;
            for(uint32_t k=0;k<4u;++k) {
                uint64_t a,b;int result=edge_key(&o,&q->vertex[k],&q->vertex[(k+1u)%4u],&a,&b);
                if(result<0)return DOK_RENDER_SCENE_ARGUMENT;
                if(result){
                    if(count+2u>11264u) {
                        qsort(w->edge_points,count,sizeof(uint64_t),key_compare);
                        uint32_t unique=0;for(uint32_t v=0;v<count;++v)if(!v || w->edge_points[v]!=w->edge_points[v-1u])w->edge_points[unique++]=w->edge_points[v];
                        count=unique;if(count+2u>11264u)return DOK_RENDER_SCENE_LIMIT;
                    }
                    w->edge_points[count++]=a;w->edge_points[count++]=b;
                }
            }
        }
    }
    qsort(w->edge_points,count,sizeof(uint64_t),key_compare);
    uint32_t unique=0;for(i=0;i<count;++i)if(!i || w->edge_points[i]!=w->edge_points[i-1u])w->edge_points[unique++]=w->edge_points[i];
    for(i=0;i<source;++i) {
        uint32_t n=weld_piece(&s->quads[i],&w->origins[i],w->edge_points,unique,w->pieces);
        if(n==UINT32_MAX || n>DOK_RENDER_QUADS-total)return DOK_RENDER_SCENE_LIMIT;
        w->origins[i].pieces=n;total+=n;
    }
    dest=total;
    while(source) {
        --source;dok_light_origin o=w->origins[source];uint32_t n=weld_piece(&s->quads[source],&o,w->edge_points,unique,w->pieces);dest-=n;
        for(i=0;i<n;++i){s->quads[dest+i]=w->pieces[i];s->cells[dest+i]=(dok_nds_render_cell){o.x,o.y,o.z,&s->quads[dest+i],1};}
    }
    s->counts.published_quads=total;s->frame.cell_count=total;return DOK_RENDER_SCENE_OK;
}
dok_render_scene_result dok_light_scene_apply(dok_render_scene *s,int16_t near_depth,dok_light_workspace *w,dok_light_counts *out)
{
    uint32_t i,j,source=0,total=0,kept=0,dest;dok_light_counts stats={0};
    if(!s || !w || !out || !s->leased || s->frame.camera.x < -32768 || s->frame.camera.x>32768 ||
       s->frame.camera.z < -32768 || s->frame.camera.z>32768 || s->frame.camera.y < -4096 || s->frame.camera.y>4096 ||
       s->frame.camera.sin_q15 < -32768 || s->frame.camera.sin_q15>32768 ||
       s->frame.camera.cos_q15 < -32768 || s->frame.camera.cos_q15>32768 ||
       s->counts.published_quads>DOK_RENDER_QUADS || s->frame.cell_count>DOK_RENDER_CELLS)
        return DOK_RENDER_SCENE_ARGUMENT;
    for(i=0;i<s->frame.cell_count;++i) {
        const dok_nds_render_cell *cell=&s->cells[i];
        if(cell->origin_x < -32768 || cell->origin_x>32768 || cell->origin_z < -32768 || cell->origin_z>32768 ||
           cell->origin_y < -4096 || cell->origin_y>4096)return DOK_RENDER_SCENE_ARGUMENT;
        for(j=0;j<cell->quad_count;++j) {
            uint32_t n;
            if(source==DOK_RENDER_QUADS || &cell->quads[j]!=&s->quads[source])return DOK_RENDER_SCENE_ARGUMENT;
            for(uint32_t k=0;k<4u;++k) {
                const dok_nds_render_vertex *v=&cell->quads[j].vertex[k];
                if(v->x < -4096 || v->x>4095 || v->y < -4096 || v->y>4095 || v->z < -4096 || v->z>4095 ||
                   v->u_q16<0 || v->u_q16>134213632 || v->v_q16<0 || v->v_q16>134213632)
                    return DOK_RENDER_SCENE_ARGUMENT;
            }
            w->origins[source]=(dok_light_origin){cell->origin_x,cell->origin_y,cell->origin_z,0};
            n=pieces(&s->quads[source],&s->frame.camera,&w->origins[source],near_depth,w->pieces);
            if(n==UINT32_MAX || n>DOK_RENDER_QUADS-total)return DOK_RENDER_SCENE_LIMIT;
            w->origins[source].pieces=n;total+=n;++source;
            if(!n)++stats.near_hidden;
            if(n>1u)++stats.split_quads;
        }
    }
    if(source!=s->counts.published_quads)return DOK_RENDER_SCENE_ARGUMENT;
    stats.source_quads=source;stats.published_quads=total;
    /* First remove only already-near-clipped primitives, then expand backward.
     * Every kept primitive emits >=1quad; unread input cannot be overwritten. */
    for(i=0;i<source;++i)if(w->origins[i].pieces) {s->quads[kept]=s->quads[i];w->origins[kept++]=w->origins[i];}
    dest=total;
    while(kept) {
        uint32_t n;dok_light_origin origin=w->origins[--kept];
        n=pieces(&s->quads[kept],&s->frame.camera,&origin,near_depth,w->pieces);dest-=n;
        for(i=0;i<n;++i) {
            s->quads[dest+i]=w->pieces[i];
            s->cells[dest+i]=(dok_nds_render_cell){origin.x,origin.y,origin.z,&s->quads[dest+i],1};
            ++stats.rows[w->pieces[i].reserved];
        }
    }
    s->counts.published_quads=total;s->frame.cell_count=total;
    dok_render_scene_result wr=weld(s,w);if(wr!=DOK_RENDER_SCENE_OK)return wr;
    total=s->counts.published_quads;stats.published_quads=total;
    memset(stats.rows,0,sizeof(stats.rows));for(i=0;i<total;++i)++stats.rows[s->quads[i].reserved];
    uint32_t cells=0;
    for(i=0;i<total;++i) {
        dok_nds_render_cell cell=s->cells[i];
        if(cells && s->cells[cells-1u].origin_x==cell.origin_x && s->cells[cells-1u].origin_y==cell.origin_y &&
           s->cells[cells-1u].origin_z==cell.origin_z &&
           s->cells[cells-1u].quads+s->cells[cells-1u].quad_count==cell.quads)
            ++s->cells[cells-1u].quad_count;
        else s->cells[cells++]=cell;
    }
    s->counts.published_quads=total;s->frame.cell_count=cells;*out=stats;return DOK_RENDER_SCENE_OK;
}
