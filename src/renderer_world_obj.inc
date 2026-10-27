/* Native direct-source OBJ trial. CPU foreground remains nearer; hardware
 * ownership publishes coverage before the remaining front-to-back CPU draw. */
#include "dok/gba_world_obj.h"
_Static_assert(sizeof(dok_gba_world_obj_state)<=DOK_WORLD_OBJ_SCRATCH,"OBJ scratch capacity");
typedef struct {uint32_t first;uint16_t width,height,cols,flags;} world_obj_record;
extern const world_obj_record dok_world_obj_records[];
extern const uint32_t dok_world_obj_patch_offsets[];
extern const uint8_t dok_world_obj_bank[];
dok_gba_world_obj_state *g_world_obj_state;
volatile uint32_t g_world_obj_work[12]; /* faces,patches,offloaded,edge fixups,cache/full,matrix/full,scan/full,other,attempted,upload bytes,upload ticks,mask pixels */
volatile uint32_t g_world_obj_trace[16]; /* enable,x,logical y,count; last new owner descriptor */
volatile uint32_t g_world_obj_ticks[4]; /* face total,opaque coverage,masked coverage,reserved; enabled by g_render_work[0] */
static DOK_COLD int world_obj_round(int n,int d) {
    if(d<0){n=-n;d=-d;}return n>=0?(n+d/2)/d:-((-n+d/2-1)/d);
}
static DOK_COLD void world_obj_begin(dok_renderer_workspace *w,unsigned view,unsigned resources) {
    /* Mode4 owns the whole legacy interval array and may overwrite this tail
     * without touching its first magic word. Do not reuse any state across it. */
    if(!g_hw_tile_active){g_world_obj_state=0;return;}
    dok_gba_world_obj_state *o=(void *)(((uintptr_t)w->covered+sizeof(w->covered)-DOK_WORLD_OBJ_SCRATCH)&~(uintptr_t)3);
    if(g_world_obj_state!=o || o->magic!=DOK_WORLD_OBJ_MAGIC) {
        zero(o,sizeof(*o));o->magic=DOK_WORLD_OBJ_MAGIC;
        for(unsigned y=0;y<128;++y)o->first_y[y]=255;
        for(unsigned y=0;y<144;++y){unsigned row=y/9,sy=row*8+(unsigned)((((int)(y%9)-4)*227+1024)>>8);
            if(o->first_y[sy]==255)o->first_y[sy]=(uint8_t)y;o->last_y[sy]=(uint8_t)y;}
    }
    g_world_obj_state=o;o->count=o->matrices=o->scan_max=0;o->view=view;o->stride=resources+1;
    zero(o->used,sizeof(o->used));zero(o->scanline,sizeof(o->scanline));
    for(unsigned i=0;i<128;++i){o->oam[i][0]=0x200;o->oam[i][1]=o->oam[i][2]=o->oam[i][3]=0;}
    o->oam[0][3]=128;o->oam[3][3]=227;
    for(unsigned i=0;i<12;++i)g_world_obj_work[i]=0;
    for(unsigned i=0;i<4;++i)g_world_obj_ticks[i]=0;
}
static unsigned world_obj_texel(const uint8_t *tile,unsigned u,unsigned v) {
    return tile[(v>>3)*128+(u>>3)*64+(v&7)*8+(u&7)];
}
static DOK_NOINLINE unsigned world_obj_mask(dok_renderer_workspace *w,const uint8_t *tile,
    int left,int top,int pa,int pd,int canvas_x,int canvas_y,int first,int last,unsigned begin,unsigned end) {
    dok_gba_world_obj_state *o=g_world_obj_state;unsigned wrote=0,scanned=0,edges=0;
    for(int x=first;x<=last;x+=2)for(unsigned y=begin;y<=end;++y) {
        if(coverage_contains((unsigned)x,y,y))continue;
        unsigned any=0,all=1,color=0;
        for(unsigned yy=o->first_y[y];yy<=o->last_y[y];++yy)for(int xx=x;xx<=x+1;++xx) {
            int u=(pa*(xx-left-canvas_x/2)>>8)+8,v=(pd*((int)yy-top-canvas_y/2)>>8)+16;
            unsigned c=0;
            if(xx>=left && xx<left+canvas_x && (int)yy>=top && (int)yy<top+canvas_y && u>=0 && u<16 && v>=0 && v<32)
                c=world_obj_texel(tile,(unsigned)u,(unsigned)v);
            if(c){any=1;if(!color)color=c;}else all=0;
        }
        ++scanned;
        if(!any)continue;
        if(!all){*scene_pointer(w->frame,(unsigned)x,y)=(uint8_t)color;++edges;}
        COVER_MARK((unsigned)x,y,y);COVER_COUNT(w,(unsigned)x)|=128;++wrote;
    }
    g_world_obj_work[3]+=edges;g_world_obj_work[11]+=scanned;return wrote;
}
static DOK_COLD void world_obj_patch(dok_renderer_workspace *w,const uint8_t *tile,
    int left,int top,int pa,int pd,unsigned dbl,unsigned solid,dok_renderer_stats *stats) {
    dok_gba_world_obj_state *o=g_world_obj_state;int canvas_x=dbl?32:16,canvas_y=dbl?64:32;
    int x0=clamp(left,0,239),x1=clamp(left+canvas_x-1,0,239);
    int y0=clamp(top,0,143),y1=clamp(top+canvas_y-1,0,143);
    if(left>=240 || left+canvas_x<=0 || top>=144 || top+canvas_y<=0)return;
    int canvas_y0=y0,canvas_y1=y1;
    while(x0<=x1){int u=(pa*(x0-left-canvas_x/2)>>8)+8;if(u>=0 && u<16)break;++x0;}
    while(x1>=x0){int u=(pa*(x1-left-canvas_x/2)>>8)+8;if(u>=0 && u<16)break;--x1;}
    while(y0<=y1){int v=(pd*(y0-top-canvas_y/2)>>8)+16;if(v>=0 && v<32)break;++y0;}
    while(y1>=y0){int v=(pd*(y1-top-canvas_y/2)>>8)+16;if(v>=0 && v<32)break;--y1;}
    if(x0>x1 || y0>y1)return;
    int first=x0&~1,last=x1&~1;
    unsigned begin=0,end=127;
    while(begin<128 && o->last_y[begin]<y0)++begin;
    while(end && o->first_y[end]>y1)--end;
    unsigned visible=0;
    for(int x=first;x<=last;x+=2)if(!coverage_contains((unsigned)x,begin,end)){visible=1;break;}
    if(!visible)return;
    unsigned slot=DOK_WORLD_OBJ_SLOTS,matrix=30;
    for(unsigned i=0;i<DOK_WORLD_OBJ_SLOTS;++i)if(o->slots[i]==tile){slot=i;break;}
    if(slot==DOK_WORLD_OBJ_SLOTS)for(unsigned i=0;i<DOK_WORLD_OBJ_SLOTS;++i)if(!o->used[i]){slot=i;break;}
    if(slot==DOK_WORLD_OBJ_SLOTS || o->count>=120){++g_world_obj_work[4];return;}
    for(unsigned i=0;i<o->matrices;++i)if(o->matrix[i][0]==pa && o->matrix[i][3]==pd){matrix=i;break;}
    if(matrix==30){if(o->matrices==30){++g_world_obj_work[5];return;}matrix=o->matrices;}
    /* Four CPU32x8 double-size bands, OAM search allowance, then the new
     * affine canvases. This conservative limit prevents PPU loss, including
     * source-transparent rows that still consume sprite evaluation work. */
    unsigned cost=(unsigned)canvas_x*2+10;
    unsigned budget=954-552-2*(9+o->count)-16;
    if(o->scan_max>budget){++g_world_obj_work[6];return;}
    for(unsigned y=(unsigned)canvas_y0/8;y<=(unsigned)canvas_y1/8;++y)
        if(o->scanline[y]+cost>budget){++g_world_obj_work[6];return;}
    unsigned trace=g_world_obj_trace[0] && g_world_obj_trace[1]<240 && g_world_obj_trace[2]<128 &&
        !coverage_contains(g_world_obj_trace[1],g_world_obj_trace[2],g_world_obj_trace[2]);
    uint32_t coverage_stamp=g_render_work[0]?render_ticks():0;
    unsigned wrote=0;
    if(solid) {
        unsigned lo=begin+(o->first_y[begin]<y0),hi=end-(o->last_y[end]>y1);
        for(int x=first;x<=last;x+=2) {
            unsigned full_x=x>=x0 && x+1<=x1;
            if(full_x && lo<=hi) {
                if(!coverage_contains((unsigned)x,lo,hi)){COVER_MARK((unsigned)x,lo,hi);COVER_COUNT(w,(unsigned)x)|=128;wrote+=hi-lo+1;}
            }
            for(unsigned y=begin;y<=end;y=full_x?(y==begin && end!=begin?end:end+1):y+1) {
                if(full_x && y>=lo && y<=hi)continue;
                if(coverage_contains((unsigned)x,y,y))continue;
                int xx=x<x0?x0:x,yy=o->first_y[y]<y0?y0:o->first_y[y];
                int u=(pa*(xx-left-canvas_x/2)>>8)+8,v=(pd*(yy-top-canvas_y/2)>>8)+16;
                *scene_pointer(w->frame,(unsigned)x,y)=(uint8_t)world_obj_texel(tile,(unsigned)u,(unsigned)v);
                COVER_MARK((unsigned)x,y,y);COVER_COUNT(w,(unsigned)x)|=128;++wrote;++g_world_obj_work[3];
            }
        }
    } else {
    wrote=world_obj_mask(w,tile,left,top,pa,pd,canvas_x,canvas_y,first,last,begin,end);
    }
    if(g_render_work[0])g_world_obj_ticks[solid?1:2]+=render_ticks()-coverage_stamp;
    if(!wrote)return;
    if(trace && coverage_contains(g_world_obj_trace[1],g_world_obj_trace[2],g_world_obj_trace[2])) {
        ++g_world_obj_trace[3];g_world_obj_trace[4]=(uintptr_t)tile;
        g_world_obj_trace[5]=(unsigned)left;g_world_obj_trace[6]=(unsigned)top;
        g_world_obj_trace[7]=(unsigned)pa;g_world_obj_trace[8]=(unsigned)pd;
        g_world_obj_trace[9]=dbl;g_world_obj_trace[10]=solid;g_world_obj_trace[11]=8+o->count;
        g_world_obj_trace[12]=begin;g_world_obj_trace[13]=end;g_world_obj_trace[14]=(unsigned)y0;g_world_obj_trace[15]=(unsigned)y1;
    }
    for(unsigned y=(unsigned)canvas_y0/8;y<=(unsigned)canvas_y1/8;++y) {
        o->scanline[y]=(uint16_t)(o->scanline[y]+cost);
        if(o->scanline[y]>o->scan_max)o->scan_max=o->scanline[y];
    }
    o->slots[slot]=tile;o->used[slot]=1;
    if(matrix==o->matrices){o->matrix[matrix][0]=(int16_t)pa;o->matrix[matrix][1]=o->matrix[matrix][2]=0;o->matrix[matrix][3]=(int16_t)pd;++o->matrices;}
    unsigned mi=matrix+2,id=8+o->count++;
    o->oam[id][0]=(uint16_t)(0xa100|(dbl?0x200:0)|(top&255));
    o->oam[id][1]=(uint16_t)(0x8000|(mi<<9)|(left&511));
    o->oam[id][2]=(uint16_t)(0x400|512|slot*16);
    for(unsigned k=0;k<4;++k)o->oam[mi*4+k][3]=(uint16_t)o->matrix[matrix][k];
    ++g_world_obj_work[1];g_world_obj_work[2]+=wrote;stats->pixels_written+=wrote;
}
static DOK_COLD int world_obj_u(const dok_renderer_face *f,int u,int q0,int q1) {
    int denominator=q1*(f->u1-u)+q0*(u-f->u0);
    if(!denominator)return INT_MAX;
    return world_obj_round((f->x1-f->x0)*q0*(u-f->u0),denominator);
}
static DOK_COLD void world_obj_face_impl(dok_renderer_workspace *w,const dok_renderer_face *f,
    const dok_renderer_image *im,unsigned resource_count,dok_renderer_stats *stats) {
    ++g_world_obj_work[8];
    if(!g_hw_tile_active || f->depth0<0 || f->depth1<0 ||
       f->v0 || !im->columns || !im->resource_id ||
       !((f->u0==0 && f->u1==im->width-1) || (f->u1==0 && f->u0==im->width-1))) {++g_world_obj_work[7];return;}
    const world_obj_record *rec=&dok_world_obj_records[g_world_obj_state->view*(resource_count+1)+im->resource_id];
    if(rec->width!=im->width || rec->height!=im->height || rec->flags!=(im->flags&8)){++g_world_obj_work[7];return;}
    int dx=f->x1-f->x0;
    if(dx<=0 || dx>16384 || im->width<2 || im->height<2)return;
    unsigned rows=(im->height+31)/32,before=g_world_obj_work[1];
    int q0=(6553600/(f->depth0+200))>>8,q1=(6553600/(f->depth1+200))>>8;
    if(q0<1)q0=1;if(q1<1)q1=1;
    for(unsigned sx=0;sx<im->width;sx+=16) {
        if(g_world_obj_state->scan_max>954-552-2*(9+g_world_obj_state->count)-16)break;
        int center=world_obj_u(f,(int)sx+8,q0,q1);
        int xa=world_obj_u(f,(int)sx,q0,q1),xb=world_obj_u(f,(int)sx+16,q0,q1);
        if(center==INT_MAX || xa==INT_MAX || xb==INT_MAX || xa==xb || center< -16384 || center>32768)continue;
        int top=world_obj_round((f->top0+(f->top1-f->top0)*center/dx)*9,8);
        int bottom=world_obj_round((f->bottom0+(f->bottom1-f->bottom0)*center/dx)*9,8);
        if(bottom<=top)continue;
        int pa=world_obj_round(16*256,xb-xa),pd=world_obj_round((im->height-1)*256,bottom-top);
        if(pa>32767 || pa< -32767 || (pa<128 && pa> -128) || pd<128 || pd>32767){++g_world_obj_work[7];continue;}
        unsigned dbl=pa<256 && pa> -256;if(pd<256)dbl=1;
        int depth=f->depth0+(f->depth1-f->depth0)*center/dx;
        unsigned light=light_index(depth);
        const uint32_t *offsets=&dok_world_obj_patch_offsets[rec->first+light*rows*rec->cols];
        int left=f->x0+center-(dbl?16:8);
        for(unsigned sy=0;sy<im->height;sy+=32) {
            int y=top+world_obj_round(((int)sy+16)*256,pd)-(dbl?32:16);
            unsigned value=offsets[sy/32*rec->cols+sx/16];
            world_obj_patch(w,dok_world_obj_bank+(value&~1u),left,y,pa,pd,dbl,value&1,stats);
        }
    }
    if(g_world_obj_work[1]!=before){++g_world_obj_work[0];++stats->faces_drawn;}
}
static DOK_COLD void world_obj_face(dok_renderer_workspace *w,const dok_renderer_face *f,
    const dok_renderer_image *im,unsigned resource_count,dok_renderer_stats *stats) {
    uint32_t stamp=g_render_work[0]?render_ticks():0;
    world_obj_face_impl(w,f,im,resource_count,stats);
    if(g_render_work[0])g_world_obj_ticks[0]+=render_ticks()-stamp;
}
