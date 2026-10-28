/* Experimental opaque static-wall ray pass. Geometry and simulation remain
 * separate: moving doors, masked class2, grates and all sprites keep faces.
 * Projection is the native depth+200 lens, so each ray begins on its near
 * plane, at camera + right*(native_screen_x-159), rather than at the eye. */
static uint8_t ray_eligible[256];
static struct {int32_t depth;int16_t top,bottom;} ray_wall[120];
static int ray_clip_lo=144,ray_clip_hi=-1;
volatile uint32_t g_ray_work[4]; /* enable, draw ticks, rays that hit, map steps */
static DOK_NOINLINE unsigned mapped_column(uint8_t *restrict,const uint8_t *restrict,unsigned,unsigned,unsigned,unsigned);

static DOK_COLD dok_renderer_status ray_prepare(dok_renderer_workspace *w,const dok_runtime *r,
    const dok_rom_assets *a,unsigned level,unsigned language) {
    zero(ray_eligible,sizeof(ray_eligible));
    for(unsigned id=0;id<r->object_count;++id) if(r->level->definitions[id].class_id==2) {
        dok_renderer_status status=image(w,r,a,level,language,(uint16_t)id);if(status) return status;
        const dok_renderer_image *im=&w->images[id];
        int scale=r->level->definitions[id].width_parameter;
        if(!(im->flags&8) && im->width*scale==480 && im->height*scale>=960 && im->columns)
            ray_eligible[id]=1;
    }
    return DOK_RENDER_OK;
}
static DOK_NOINLINE void ray_draw(dok_renderer_workspace *w,const dok_runtime *r,
    const dok_projection_camera *c,dok_renderer_stats *stats) {
    uint32_t stamp=g_ray_work[0]?render_ticks():0;
    for(unsigned k=1;k<4;++k) g_ray_work[k]=0;
    const int *envelope=world_shared_envelope;
    for(unsigned x=0;x<240;x+=2) {
        unsigned column=x>>1;ray_wall[column].depth=INT_MAX;ray_wall[column].top=144;ray_wall[column].bottom=-1;
        int lateral0=(int)x*4/3-159;
        int32_t ox=c->x*65536+c->cosine*lateral0*2;
        int32_t oz=c->z*65536+c->sine*lateral0*2;
        int32_t dx=-c->sine+c->cosine*lateral0/200;
        int32_t dz=c->cosine+c->sine*lateral0/200;
        int cx=(ox>>16)/480,cz=(oz>>16)/480;
        int sx=dx<0?-1:1,sz=dz<0?-1:1;
        unsigned ax=(unsigned)(dx<0?-dx:dx),az=(unsigned)(dz<0?-dz:dz);
        unsigned tx=ax?(unsigned)(sx>0?(cx+1)*480*65536-ox:ox-cx*480*65536)/ax:UINT_MAX;
        unsigned tz=az?(unsigned)(sz>0?(cz+1)*480*65536-oz:oz-cz*480*65536)/az:UINT_MAX;
        unsigned stepx=ax?31457280u/ax:UINT_MAX,stepz=az?31457280u/az:UINT_MAX;
        unsigned hit=0,axis=0;uint16_t object=0;
        int fromx=cx,fromz=cz;
        for(unsigned steps=0;steps<40;++steps) {
            if(g_ray_work[0]) ++g_ray_work[3];
            fromx=cx;fromz=cz;
            if(tx<tz) {hit=tx;tx+=stepx;cx+=sx;axis=0;}
            else {hit=tz;tz+=stepz;cz+=sz;axis=1;}
            if(hit>12800 || cx<envelope[0] || cx>envelope[1] || cz<envelope[2] || cz>envelope[3]) break;
            if(!object_at(r,DOK_WORLD_TMAP,cx,cz,&object)) break;
            if(ray_eligible[object]) {
                const dok_renderer_image *im=&w->images[object];
                unsigned distance=hit/2;
                if(!distance) distance=1;
                int coordinate=axis?(int)(add(ox,mul(dx,(int)hit))>>16):(int)(add(oz,mul(dz,(int)hit))>>16);
                int phase=coordinate%480;if(phase<0) phase+=480;
                if((axis && sz<0) || (!axis && sx>0)) phase=480-phase;
                unsigned u=(unsigned)(phase*(im->width-1)/480);
                uint16_t fc;int32_t inset=0;
                if(!object_at(r,DOK_WORLD_FCMAP,fromx,fromz,&fc) || !field(r->level,fc,0x54,&inset)) break;
                int scale=r->level->definitions[object].width_parameter;
                int h=word(im->height*scale-inset+1);
                int floor=-r->level->definitions[fc].height_parameter;
                int factor=6553600/((int)distance+200);
                int top=scale_y(((floor-h-c->y)*factor>>15)+79);
                int bottom=scale_y(((floor-c->y)*factor>>15)+79);
                if(bottom<0 || top>143 || bottom<=top) break;
                int first=clamp(top,0,143),last=clamp(bottom,0,143);
                unsigned v0=(unsigned)clamp(im->height*inset/960,0,im->height-1);
                unsigned step=((im->height-1-v0)<<16)/(unsigned)(bottom-top);
                unsigned v=(v0<<16)+(unsigned)(first-top)*step;
                unsigned row=light_index((int32_t)distance);
                extern const uint8_t dok_prelit_bank[];
                uint32_t off=prelit_view[im->resource_id*16+row];
                const uint8_t *pixels=dok_prelit_bank+off+u*im->height;
                stats->pixels_written+=mapped_column(w->frame+first*240+x,pixels,v,step,(unsigned)(last-first+1),0);
                ray_wall[column].depth=(int32_t)distance;ray_wall[column].top=(int16_t)first;ray_wall[column].bottom=(int16_t)last;
                if(g_ray_work[0]) ++g_ray_work[2];
                break;
            }
        }
    }
    if(g_ray_work[0]) g_ray_work[1]=render_ticks()-stamp;
}
static DOK_COLD void ray_publish(dok_renderer_workspace *w) {
    (void)w;
    for(unsigned x=0;x<240;x+=2) {
        unsigned k=x>>1;
        if(ray_wall[k].bottom>=ray_wall[k].top) {
            COVER_MARK(x,(unsigned)ray_wall[k].top,(unsigned)ray_wall[k].bottom);
            COVER_COUNT(w,x)|=128;
        }
    }
}
