/* GPU handles final alpha/ordering. Source mip columns are copied only inside
 * conservative sector clip intervals, without screen coverage or owner masks. */
#include "dok/gba_sector_gpu.h"
#ifdef DOK_RENDER_SECTOR_GPU_THUMB_PROTOTYPE
#define GPU_CODE __attribute__((target("thumb"),noinline,section(".rom.text")))
#else
#define GPU_CODE DOK_COLD
#endif
typedef struct {const uint8_t *pixels;uint32_t first;uint16_t width,height,mask,reserved;} sector_gpu_record;
extern const sector_gpu_record dok_sector_gpu_records[];
extern const uint32_t dok_sector_gpu_views[6][2],dok_sector_gpu_offsets[];
extern const uint8_t dok_sector_gpu_bank[];
extern unsigned dok_gba_sector_gpu_copy(uint8_t *,const uint8_t *,unsigned,unsigned,unsigned,const uint8_t *,unsigned,unsigned);
dok_sector_gpu_state *g_sector_gpu;
volatile uint32_t g_sector_gpu_work[12]; /* faces,pieces,source bytes,cpu fallback,missing resource,matrix,scan,atlas,near,approx,empty,reserved */
volatile uint32_t g_sector_gpu_ticks[3]; /* optional setup,source clipping/copy,reserved */
volatile uint32_t g_sector_gpu_trace;
static unsigned sector_gpu_current_face;
#ifdef DOK_RENDER_SECTOR_GPU_SHEAR_PROTOTYPE
static int sector_gpu_current_pc;
volatile uint32_t g_sector_gpu_shear_work[8]; /* candidates,pieces,max X boundary error,max native height delta,max abs PC,fit rejects,peak pixel+UI cost,rejected total */
volatile uint32_t g_sector_gpu_pressure_probe; /* Diagnostic only: ignore scan rejection, skip hard-cap commands. Never an FPS/quality mode. */
#endif
_Static_assert(sizeof(sector_state)+sizeof(dok_sector_gpu_state)+7<=sizeof(((dok_renderer_workspace *)0)->covered),"sector/GPU scratch");
static GPU_CODE void sector_gpu_begin(dok_renderer_workspace *w,const dok_runtime *r) {
    g_sector_gpu=(void *)(((uintptr_t)sector+sizeof(*sector)+3)&~(uintptr_t)3);
    (void)w;g_sector_gpu->valid=1;g_sector_gpu->failed=g_sector_gpu->count=g_sector_gpu->matrices=g_sector_gpu->bytes=0;
    g_sector_gpu->view=static_view;zero(g_sector_gpu->scan,sizeof(g_sector_gpu->scan));
    for(unsigned k=0;k<12;++k)g_sector_gpu_work[k]=0;for(unsigned k=0;k<3;++k)g_sector_gpu_ticks[k]=0;
#ifdef DOK_RENDER_SECTOR_GPU_SHEAR_PROTOTYPE
    for(unsigned k=0;k<8;++k)g_sector_gpu_shear_work[k]=0;
#endif
    for(unsigned k=0;k<128;++k){g_sector_gpu->oam[k][0]=0x200;g_sector_gpu->oam[k][1]=g_sector_gpu->oam[k][2]=g_sector_gpu->oam[k][3]=0;}
    extern unsigned g_hw_weapon[5];unsigned keys=0;for(unsigned k=0;k<3;++k)if(r->player.keys&(1u<<k))++keys;
    for(unsigned y=0;y<144;++y){unsigned cost=y<16?136:y>=128?keys*16:0;
        if(g_hw_weapon[4] && y>=g_hw_weapon[1] && y<g_hw_weapon[1]+g_hw_weapon[3]*8)cost+=g_hw_weapon[2]*8;
        g_sector_gpu->ui_cost[y]=(uint16_t)cost;}
#ifdef DOK_RENDER_SECTOR_GPU_SHEAR_PROTOTYPE
    for(unsigned y=0;y<144;++y)if(g_sector_gpu->ui_cost[y]>g_sector_gpu_shear_work[6])g_sector_gpu_shear_work[6]=g_sector_gpu->ui_cost[y];
#endif
}
static GPU_CODE int sector_gpu_round(int n,int d) {
    if(d<0){d=-d;n=-n;}return n>=0?(n+d/2)/d:-((-n+d/2-1)/d);
}
static GPU_CODE int sector_gpu_inverse(const dok_renderer_face *f,int u,int q0,int q1) {
    int denominator=q1*(f->u1-u)+q0*(u-f->u0);
    if(!denominator)return INT_MAX;
    return f->x0+sector_gpu_round((f->x1-f->x0)*q0*(u-f->u0),denominator);
}
static GPU_CODE unsigned sector_gpu_cut(unsigned x,int z) {
    unsigned cutoff=0;
    for(unsigned rank=sector->counts[x];rank>0;--rank) {
        const sector_history *h=&sector_history_rows[x][rank-1];if((h->near&32767)>(unsigned)z)continue;
        cutoff=sector->history_top[x][rank-1];
        if((unsigned)z<h->far){cutoff=(unsigned)clamp(sector_y(-561,(unsigned)z)+1,0,128);
            if(rank>1 && cutoff<sector->history_top[x][rank-2])cutoff=sector->history_top[x][rank-2];}
        break;
    }
    return cutoff;
}
static GPU_CODE unsigned sector_gpu_piece(dok_renderer_workspace *w,const uint8_t *source,unsigned source_width,
    unsigned sx,unsigned sy,unsigned width,unsigned height,int center_x,int center_y,int pa,int pd,const dok_renderer_face *f,
    const uint8_t *lut,unsigned black) {
    (void)w;dok_sector_gpu_state *g=g_sector_gpu;
    unsigned base_w=width<=8?8:width<=16?16:width<=32?32:64;
    unsigned base_h=base_w>=32?64:base_w==16?32:height<=8?8:height<=16?16:32;
    unsigned texture_bytes=base_h*base_w;
    unsigned dbl=(pa<256 && pa> -256)||pd<256;
#ifdef DOK_RENDER_SECTOR_GPU_SHEAR_PROTOTYPE
    int pc=sector_gpu_current_pc,data_x=center_x;
    unsigned abs_pa=(unsigned)(pa<0?-pa:pa),abs_pc=(unsigned)(pc<0?-pc:pc);
    unsigned painted_width=(width*256+abs_pa-1)/abs_pa;
    unsigned painted_height=(height*256+abs_pc*painted_width+(unsigned)pd-1)/(unsigned)pd;
    while(base_w==8 && base_h<32 && painted_height>base_h*2-2)base_h*=2;
    if(painted_height>base_h*2-2){++g_sector_gpu_work[8];return 0;}
    texture_bytes=base_h*base_w;
    unsigned px=(base_w-width)/2,py=(base_h-height)/2;
    center_x-=sector_gpu_round(((int)px+(int)width/2-(int)base_w/2)*256,pa);
    center_y+=sector_gpu_round(pc*(data_x-center_x)-((int)py+(int)height/2-(int)base_h/2)*256,pd);
    if(pc)dbl=1;
#endif
    unsigned canvas_x=dbl?base_w*2:base_w,canvas_y=dbl?base_h*2:base_h;
    int left=center_x-(int)canvas_x/2,top=center_y-(int)canvas_y/2;
    if(left>=240 || left+(int)canvas_x<=0 || top>=144 || top+(int)canvas_y<=0)return 1;
    unsigned matrix=g->matrices;
    for(unsigned k=0;k<g->matrices;++k)if(g->matrix[k][0]==pa && g->matrix[k][1]==pd
#ifdef DOK_RENDER_SECTOR_GPU_SHEAR_PROTOTYPE
        && g->pc[k]==pc
#endif
        ){matrix=k;break;}
    if(g->count==64 || g->bytes+texture_bytes>sizeof(g->texture)){++g_sector_gpu_work[7];
#ifdef DOK_RENDER_SECTOR_GPU_SHEAR_PROTOTYPE
        if(g_sector_gpu_pressure_probe)return 1;
#endif
        return 0;}
    /*1210-cycle OBJ period: no HBlank OAM/DMA3 multiplexer. Native UI bounds
     * are available before scene rendering; all128 OAM searches reserved. */
    unsigned cost=canvas_x*2+
#ifdef DOK_RENDER_SECTOR_GPU_SHEAR_PROTOTYPE
        8; /* OAM search is accounted separately, not again per affine OBJ. */
#else
        10;
#endif
    uint8_t *dst=g->texture+g->bytes;zero(dst,texture_bytes);unsigned nonempty=0;
    unsigned pad_x=(base_w-width)/2,pad_y=(base_h-height)/2;
    int16_t range_first[64],range_last[64];
    for(unsigned k=0;k<base_w;++k){range_first[k]=240;range_last[k]=-1;}
    for(int physical_x=clamp(left,0,239);physical_x<=clamp(left+(int)canvas_x-1,0,239);++physical_x){
        int tex_x=(pa*(physical_x-center_x)>>8)+(int)base_w/2;
        if(tex_x<0 || tex_x>=(int)base_w)continue;
        if(range_first[tex_x]==240)range_first[tex_x]=(int16_t)physical_x;range_last[tex_x]=(int16_t)physical_x;
    }
    for(unsigned col=0;col<width;++col) {
#ifdef DOK_RENDER_SECTOR_GPU_SHEAR_PROTOTYPE
        int first=(int)pad_y,last=(int)(pad_y+height)-1,deny_first=(int)base_h,deny_last=-1;unsigned used=0;
        for(int physical_x=range_first[pad_x+col];physical_x<=range_last[pad_x+col];++physical_x) {
            int xx=physical_x&~1;
            if(xx<f->x0 || xx>f->x1){first=(int)base_h;continue;}
            used=1;unsigned x=(unsigned)xx/2;
            int shear=pc*(physical_x-center_x),cut_y=(g->clip_top[x]*256+227)/228;
            int lo=((shear+pd*(cut_y-center_y)+255)>>8)+(int)base_h/2;
            int after=(int)(((g->clip_bottom[x]+1)*256+227)/228);
            int hi=((shear+pd*(after-center_y))>>8)+(int)base_h/2-1;
            if(lo>first)first=lo;if(hi<last)last=hi;
            if(g->deny_first[x]<=g->deny_last[x]) {
                int wall_lo=g->deny_first[x]*256/228,wall_hi=((g->deny_last[x]+1)*256+227)/228-1;
                lo=((shear+pd*(wall_lo-center_y))>>8)+(int)base_h/2;
                hi=((shear+pd*(wall_hi-center_y))>>8)+(int)base_h/2;
                if(lo<deny_first)deny_first=lo;if(hi>deny_last)deny_last=hi;
            }
        }
        if(!used || first>last)continue;
#else
        unsigned cutoff=0,wall_first=128,wall_last=0,bottom_clip=127,used=0;int wall=0;
        for(int physical_x=range_first[pad_x+col];physical_x<=range_last[pad_x+col];++physical_x) {
            int xx=physical_x&~1;
            if(xx<f->x0 || xx>f->x1){cutoff=128;continue;}
            used=1;
            unsigned x=(unsigned)xx/2;
            if(g->clip_top[x]>cutoff)cutoff=g->clip_top[x];
            if(g->clip_bottom[x]<bottom_clip)bottom_clip=g->clip_bottom[x];
            if(g->deny_first[x]<=g->deny_last[x]){wall=1;if(g->deny_first[x]<wall_first)wall_first=g->deny_first[x];if(g->deny_last[x]>wall_last)wall_last=g->deny_last[x];}
        }
        if(!used || cutoff>bottom_clip)continue;
        int physical_cut=(int)(cutoff*256+227)/228;
        int first=((pd*(physical_cut-center_y)+255)>>8)+(int)base_h/2;
        if(first<(int)pad_y)first=(int)pad_y;
        int last=(int)(pad_y+height)-1;
        int bottom_source=(pd*((int)(((bottom_clip+1)*256+227)/228)-center_y)>>8)+(int)base_h/2-1;
        if(last>bottom_source)last=bottom_source;
        int deny_first=(int)base_h,deny_last=-1;
        if(wall){int lo=(int)(wall_first*256)/228,hi=(int)(((wall_last+1)*256+227)/228)-1;
            deny_first=(pd*(lo-center_y)>>8)+(int)base_h/2;deny_last=(pd*(hi-center_y)>>8)+(int)base_h/2;}
#endif
        for(unsigned part=0;part<2;++part) {
            int lo=first,hi=last;
            if(part==0 && deny_last>=deny_first){if(hi>=deny_first)hi=deny_first-1;}
            else if(part==1){if(deny_last<deny_first)continue;if(lo<=deny_last)lo=deny_last+1;}
            if(lo>hi)continue;
            unsigned source_y=sy+(unsigned)lo-pad_y,source_x=sx+col;
            const uint8_t *pixels=source+(source_y>>3)*source_width*8+(source_x>>3)*64+(source_y&7)*8+(source_x&7);
            unsigned out_x=pad_x+col;uint8_t *output=dst+((unsigned)lo>>3)*base_w*8+(out_x>>3)*64+((unsigned)lo&7)*8+(out_x&7);
            nonempty|=dok_gba_sector_gpu_copy(output,pixels,(unsigned)(hi-lo+1),(source_y&7)|(((unsigned)lo&7)<<4),(source_width-8)*8,lut,black,(base_w-8)*8);
            g_sector_gpu_work[2]+=(unsigned)(hi-lo+1);
        }
    }
    if(!nonempty){++g_sector_gpu_work[10];return 1;}
    if(matrix==32){++g_sector_gpu_work[5];
#ifdef DOK_RENDER_SECTOR_GPU_SHEAR_PROTOTYPE
        if(g_sector_gpu_pressure_probe)return 1;
#endif
        return 0;}
#ifdef DOK_RENDER_SECTOR_GPU_SHEAR_PROTOTYPE
    unsigned peak=g_sector_gpu_shear_work[6];
    for(int y=clamp(top,0,143);y<=clamp(top+(int)canvas_y-1,0,143);++y){
        unsigned value=g->scan[y]+cost+g->ui_cost[y];if(value>peak)peak=value;}
    /* Active GPU entries occupy64..64+count. Searching through the last
     * new entry costs at most2*(65+count), including disabled UI gaps.
     * Recheck the peak of ALL rows as that search cost grows. */
    unsigned total=peak+2*(65+g->count);
    if(total>1210){g_sector_gpu_shear_work[7]=total;++g_sector_gpu_work[6];if(!g_sector_gpu_pressure_probe)return 0;}
    g_sector_gpu_shear_work[6]=peak;
#else
    for(int y=clamp(top,0,143);y<=clamp(top+(int)canvas_y-1,0,143);++y)if(g->scan[y]+cost+g->ui_cost[y]+256>1210){++g_sector_gpu_work[6];return 0;}
#endif
    if(matrix==g->matrices){g->matrix[matrix][0]=(int16_t)pa;g->matrix[matrix][1]=(int16_t)pd;
#ifdef DOK_RENDER_SECTOR_GPU_SHEAR_PROTOTYPE
        g->pc[matrix]=(int16_t)pc;
#endif
        ++g->matrices;}
    unsigned id=g->count++;
    unsigned shape=(base_w==base_h)?0:0x8000,size=base_w==64||base_w==32?3:base_w==16?2:base_h==32?1:0;
    g->oam[id][0]=(uint16_t)(0x2100|shape|(dbl?0x200:0)|(top&255));
    g->oam[id][1]=(uint16_t)((size<<14)|(matrix<<9)|(left&511));
    g->oam[id][2]=(uint16_t)(0x400|g->bytes/32);g->bytes+=texture_bytes;
    g->patch[id]=(dok_sector_gpu_patch){source,(uint16_t)sector_gpu_current_face,(uint16_t)sx,(uint16_t)sy,
        (uint8_t)width,(uint8_t)height,(uint8_t)source_width,(uint8_t)((lut-w->lights)/256),(uint8_t)black,0};
    for(int y=clamp(top,0,143);y<=clamp(top+(int)canvas_y-1,0,143);++y)g->scan[y]+=(uint16_t)cost;
    ++g_sector_gpu_work[1];return 1;
}
#ifdef DOK_RENDER_SECTOR_GPU_SHEAR_PROTOTYPE
#include "renderer_sector_gpu_shear.inc"
#endif
static GPU_CODE unsigned sector_gpu_face(dok_renderer_workspace *w,const dok_runtime *r,const dok_renderer_face *input) {
    dok_renderer_face f=*input;
    if(f.x0>f.x1){swap32(&f.x0,&f.x1);swap32(&f.top0,&f.top1);swap32(&f.bottom0,&f.bottom1);swap32(&f.depth0,&f.depth1);swap32(&f.u0,&f.u1);}
    if(f.x0==f.x1 || f.x1<0 || f.x0>239)return 1;
    if(f.depth0<0 || f.depth1<0 || f.u0==f.u1){++g_sector_gpu_work[8];return 0;}
    int dx=f.x1-f.x0,q0=sector_div(6553600,(unsigned)(f.depth0+200))>>8,q1=sector_div(6553600,(unsigned)(f.depth1+200))>>8;
    if(q0<1)q0=1;if(q1<1)q1=1;
    unsigned any=0;
    for(int xx=(clamp(f.x0,0,239)+1)&~1;xx<=clamp(f.x1,0,239);xx+=2) {
        unsigned x=(unsigned)xx/2;int i=xx-f.x0,n0=q0*(dx-i),n1=q1*i,z=(f.depth0*n0+f.depth1*n1)/(n0+n1);
        if(z<0)z=0;int top=f.top0+(f.top1-f.top0)*i/dx,bottom=f.bottom0+(f.bottom1-f.bottom0)*i/dx;
        unsigned cut=sector_gpu_cut(x,z);if(top<(int)cut)top=(int)cut;
        unsigned first=128,last=0;
        if((unsigned)z>=sector->wall_z[x]){first=sector->wall_first[x];last=sector->wall_last[x];}
        if(top>127 || bottom<0 || top>bottom || ((int)first<=top && (int)last>=bottom)){top=128;bottom=0;}
        else any=1;
        g_sector_gpu->clip_top[x]=(uint8_t)clamp(top,0,128);g_sector_gpu->clip_bottom[x]=(uint8_t)clamp(bottom,0,127);
        g_sector_gpu->deny_first[x]=(uint8_t)first;g_sector_gpu->deny_last[x]=(uint8_t)last;
    }
    if(!any){++g_sector_gpu_work[10];return 1;}
    const dok_renderer_image *im=face_image(w,f.image_index);
    if(im->width<2 || im->height<2){++g_sector_gpu_work[8];return 0;}
    unsigned begin=dok_sector_gpu_views[static_view][0],end=begin+dok_sector_gpu_views[static_view][1];
    unsigned stop=end;
    uintptr_t key=(uintptr_t)im->pixels;unsigned mask=im->flags&8;
    while(begin<end){unsigned mid=(begin+end)/2;const sector_gpu_record *rec=&dok_sector_gpu_records[mid];
        if((uintptr_t)rec->pixels<key || ((uintptr_t)rec->pixels==key && rec->mask<mask))begin=mid+1;else end=mid;}
    const sector_gpu_record *rec=&dok_sector_gpu_records[begin];
    if(begin==stop || (uintptr_t)rec->pixels!=key || rec->mask!=mask || rec->width!=im->width || rec->height!=im->height){++g_sector_gpu_work[4];return 0;}
    int crop_x=0,crop_y=0;
    if(f.image_index<r->object_count && r->level->definitions[f.image_index].class_id==7) {
        const dok_runtime_object *o=&r->objects[f.image_index];int extent=o->state.door.extent;if(extent<1)extent=1;
        if(im->flags&0x40)crop_x=im->width/extent*(int16_t)o->frame;
        else if(im->flags&0x100)crop_y=im->height/extent*(int16_t)o->frame;
    }
    int max_h=f.bottom0-f.top0;if(f.bottom1-f.top1>max_h)max_h=f.bottom1-f.top1;
    unsigned level=max_h<=16?0:max_h<=32?1:max_h<=64?2:3;
    unsigned width=8u<<level,height=16u<<level;
#ifdef DOK_RENDER_SECTOR_GPU_SHEAR_PROTOTYPE
    return sector_gpu_shear_face(w,&f,im,rec,q0,q1,crop_x,crop_y,level);
#endif
    /* Whole flat billboards avoid the OAM/canvas cost of texture strips.
     * Perspective panels retain adaptive narrow strips below. */
    if(f.depth0==f.depth1 && f.top0==f.top1 && f.bottom0==f.bottom1 &&
        ((f.u0==0 && f.u1==im->width-1)||(f.u1==0 && f.u0==im->width-1))) {
        int xa=sector_gpu_inverse(&f,crop_x,q0,q1),xe=sector_gpu_inverse(&f,(int)width*(im->width-1)/(int)(width-1)+crop_x,q0,q1);
        int pa=xe!=xa?sector_gpu_round((int)width*256,xe-xa):0;
        int effective=(int)(height-1)*(im->height-1-f.v0)/(im->height-1),full_h=f.bottom0-f.top0;
        int pd=full_h>0?sector_gpu_round(effective*228,full_h):0;
        if((pa>=128||pa<=-128) && pa<=32767 && pa>=-32767 && pd>0 && pd<=32767) {
            unsigned base_h=width>=32?64:width==16?32:16,dbl=(pa<256&&pa>-256)||pd<256;
            unsigned step=base_h*(dbl?2:1)*(unsigned)pd/256;if(step>base_h)step=base_h;if(!step)step=1;
            unsigned shade=light_index(f.depth0);const uint8_t *source=dok_sector_gpu_bank+dok_sector_gpu_offsets[rec->first+shade*4+level];
            for(unsigned sy=0;sy<height;sy+=step) {
                unsigned rows=height-sy;if(rows>step)rows=step;
                int source_center=(int)sy+(int)rows/2+(crop_y-f.v0)*(int)(height-1)/(im->height-1);
                int center_y=sector_gpu_round(f.top0*256,228)+sector_gpu_round(source_center*256,pd);
                unsigned pad_y=(base_h-rows)/2;
                center_y-=sector_gpu_round(((int)pad_y+(int)rows/2-(int)base_h/2)*256,pd);
                if(!sector_gpu_piece(w,source,width,0,sy,width,rows,(xa+xe)/2,center_y,pa,pd,&f,w->lights+shade*256,(im->flags&8)?0:g_hw_tile_black))return 0;
            }
            ++g_sector_gpu_work[0];return 1;
        }
    }
    for(unsigned sx=0;sx<width;) {
        int u0=(int)sx*(im->width-1)/(int)(width-1)+crop_x,u1=(int)(sx+1)*(im->width-1)/(int)(width-1)+crop_x;
        int xa=sector_gpu_inverse(&f,u0,q0,q1),xb=sector_gpu_inverse(&f,u1,q0,q1);
        if(xa==INT_MAX || xb==INT_MAX || xa==xb){++sx;continue;}
        unsigned columns=8;
        int unit=xb-xa;if(unit<0)unit=-unit;
        if(unit>16){++g_sector_gpu_work[8];return 0;}
        if((int)columns*unit>16)columns=(unsigned)(16/unit);if(!columns)columns=1;if(columns>width-sx)columns=width-sx;
        int next_u=(int)(sx+columns)*(im->width-1)/(int)(width-1)+crop_x;
        int xe=sector_gpu_inverse(&f,next_u,q0,q1);if(xe==INT_MAX || xe==xa){sx+=columns;continue;}
        int center=(xa+xe)/2,pa=sector_gpu_round((int)columns*256,xe-xa);
        if(!pa || pa>32767 || pa< -32767){++g_sector_gpu_work[8];return 0;}
        int top=f.top0+(f.top1-f.top0)*(center-f.x0)/dx,bottom=f.bottom0+(f.bottom1-f.bottom0)*(center-f.x0)/dx;
        int full_h=bottom-top;if(full_h<=0){sx+=columns;continue;}
        int effective=(int)(height-1)*(im->height-1-f.v0)/(im->height-1);
        int pd=sector_gpu_round(effective*228,full_h);
        if(pd<1 || pd>32767){++g_sector_gpu_work[8];return 0;}
        int depth=f.depth0+(f.depth1-f.depth0)*(center-f.x0)/dx;
        if(depth<0)depth=0;unsigned shade=light_index(depth);
        const uint8_t *source=dok_sector_gpu_bank+dok_sector_gpu_offsets[rec->first+shade*4+level];
        unsigned step=(unsigned)(64*pd/256);if(step<1)step=1;if(step>32)step=32;
        for(unsigned sy=0;sy<height;sy+=step) {
            unsigned rows=height-sy;if(rows>step)rows=step;
            int source_center=(int)sy+(int)rows/2+(crop_y-f.v0)*(int)(height-1)/(im->height-1);
            int center_y=sector_gpu_round(top*256,228)+sector_gpu_round(source_center*256,pd);
            unsigned base_h=rows<=8?8:rows<=16?16:32,pad_x=(8-columns)/2,pad_y=(base_h-rows)/2;
            int center_x=center-sector_gpu_round(((int)pad_x+(int)columns/2-4)*256,pa);
            center_y-=sector_gpu_round(((int)pad_y+(int)rows/2-(int)base_h/2)*256,pd);
            if(!sector_gpu_piece(w,source,width,sx,sy,columns,rows,center_x,center_y,pa,pd,&f,w->lights+shade*256,(im->flags&8)?0:g_hw_tile_black))return 0;
        }
        sx+=columns;
    }
    ++g_sector_gpu_work[0];return 1;
}
static GPU_CODE unsigned sector_gpu_dynamics(dok_renderer_workspace *w,const dok_runtime *r) {
#ifdef DOK_RENDER_SECTOR_GPU_OFF_PROTOTYPE
    g_sector_gpu->failed=1;++g_sector_gpu_work[3];return 0;
#endif
    uint32_t stamp=g_render_work[0]?render_ticks():0;
    for(unsigned rank=0;rank<w->face_count;++rank){sector_gpu_current_face=face_order[rank];if(!sector_gpu_face(w,r,&w->faces[face_order[rank]])) {
        g_sector_gpu->failed=1;g_sector_gpu->count=g_sector_gpu->matrices=g_sector_gpu->bytes=0;++g_sector_gpu_work[3];
        if(g_render_work[0])g_sector_gpu_ticks[0]=render_ticks()-stamp;return 0;
    }}
    /* Lower OAM index wins. Append order was far-to-near. */
    for(unsigned a=0,b=g_sector_gpu->count?g_sector_gpu->count-1:0;a<b;++a,--b){
        for(unsigned k=0;k<3;++k){uint16_t value=g_sector_gpu->oam[a][k];g_sector_gpu->oam[a][k]=g_sector_gpu->oam[b][k];g_sector_gpu->oam[b][k]=value;}
        dok_sector_gpu_patch value=g_sector_gpu->patch[a];g_sector_gpu->patch[a]=g_sector_gpu->patch[b];g_sector_gpu->patch[b]=value;
    }
    if(g_render_work[0])g_sector_gpu_ticks[0]=render_ticks()-stamp;return 1;
}
