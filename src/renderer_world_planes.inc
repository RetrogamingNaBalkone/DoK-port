/* Experimental shared horizontal-plane mapper. Walls, doors, actors and
 * vertical ceiling strips still use the established renderer. Floor height
 * is zero in all six current package views; distinct ceiling heights are
 * intersected nearest first. This changes the native affine UV mapping to
 * perspective world mapping, while preserving tile phase, textures and LIGHT.
 */
#ifdef DOK_RENDER_WORLD_DIV_PROTOTYPE
extern int32_t dok_gba_plane_divide(int32_t,unsigned);
/* Row depth is clipped to6400, so the Q16 cell step is at most12015.
 * The larger-divisor fallback also keeps diagnostic cameras well defined. */
static int32_t world_divide(int32_t n,unsigned d) {
    return d<=16384?dok_gba_plane_divide(n,d):n/(int32_t)d;
}
#define WORLD_DIV(n,d) world_divide((n),(unsigned)(d))
#else
#define WORLD_DIV(n,d) ((n)/(d))
#endif
typedef struct {
    const uint8_t *pixels[2];
    int16_t ceiling,shift_x,shift_y;
    uint8_t width[2],height[2],wrap,ready;
} world_plane_info;
static world_plane_info world_planes[256];
static int world_ceiling[16];
static unsigned world_ceiling_count;
#ifdef DOK_RENDER_RAY_WALLS_PROTOTYPE
static int world_shared_envelope[4];
#endif
#ifdef DOK_RENDER_PROCEDURAL_PROTOTYPE
static const uint8_t *world_procedural_colors;
typedef struct {
    uint8_t *dst;uint32_t *bits;
    const uint8_t *cells;const world_plane_info *info;
    int32_t wx,wz,dx,dz;
    uint32_t mask,count,plane;int32_t height;
    const uint8_t *lut;
} procedural_span;
_Static_assert(sizeof(world_plane_info)==20,"procedural ARM descriptor ABI");
_Static_assert(offsetof(procedural_span,lut)==48,"procedural ARM span ABI");
extern unsigned dok_gba_procedural_span(const procedural_span *);
#endif
#ifdef DOK_RENDER_WORLD_SPANS_PROTOTYPE
typedef struct {
    uint8_t *dst;uint32_t *bits;
    const uint8_t *pixels,*lut;
    int32_t wx,wz,dx,dz;
    uint32_t mask,count,width,height;
    int32_t shift_x,shift_y;
} world_texture_span;
_Static_assert(offsetof(world_texture_span,mask)==32,"world texture ARM span ABI");
extern unsigned dok_gba_world_texture_span(const world_texture_span *);
#ifdef DOK_RENDER_WATER_SPANS_PROTOTYPE
extern unsigned dok_gba_world_texture_span_shift(const world_texture_span *);
extern unsigned dok_gba_world_texture_span_wrap(const world_texture_span *);
#endif
extern unsigned dok_gba_world_skip(const uint32_t *,unsigned,unsigned);
#ifdef DOK_RENDER_WORLD_LOOKUP_PROTOTYPE
typedef struct {
    uint8_t *dst;uint32_t *bits;
    const uint8_t *cells;const world_plane_info *info;
    int32_t wx,wz,dx,dz;
    uint32_t mask,count,plane;int32_t height;
    const uint8_t *lut,*classes;
} world_lookup_span;
_Static_assert(sizeof(world_plane_info)==20,"world lookup material ABI");
_Static_assert(offsetof(world_lookup_span,classes)==52,"world lookup span ABI");
extern unsigned dok_gba_world_lookup_floor(const world_lookup_span *);
extern unsigned dok_gba_world_lookup_ceiling(const world_lookup_span *);
#endif
volatile uint32_t g_world_span_work[9]; /* enable,setup,walk,sample ticks,rows,spans,pixels,covered,water */
#endif

static DOK_COLD dok_renderer_status world_plane_register(dok_renderer_workspace *w,const dok_runtime *r,
    const dok_rom_assets *a,unsigned level,unsigned language,unsigned object) {
    if(w->surface_known[object][0]) return DOK_RENDER_OK;
    int32_t inset;if(!field(r->level,(uint16_t)object,0x54,&inset)) return DOK_RENDER_PROFILE;
    if(r->level->definitions[object].height_parameter || inset<0) return DOK_RENDER_PROFILE;
    for(unsigned plane=0;plane<2;++plane) {
        dok_renderer_status status=surface_image(w,r,a,level,language,object,plane);if(status) return status;
    }
    world_plane_info *info=&world_planes[r->level->definitions[object].glyph];
    int ceiling=inset-961;
#ifdef DOK_RENDER_MERGE_CEILINGS_PROTOTYPE
    if(inset>=0 && inset<=10) ceiling=-951;
#endif
    info->ceiling=(int16_t)ceiling;info->ready=3;info->shift_x=info->shift_y=0;info->wrap=0;
    for(unsigned p=0;p<2;++p) {
        const dok_image_view *im=&w->surface_images[object][p];
        if(im->width>256 || im->height>256) return DOK_RENDER_PROFILE;
        info->pixels[p]=im->pixels;info->width[p]=(uint8_t)(im->width-1);info->height[p]=(uint8_t)(im->height-1);
    }
    const dok_runtime_definition *def=&r->level->definitions[object];
    if(def->class_id==12 || def->class_id==13) {
        const dok_runtime_object *o=&r->objects[object];unsigned flags=def->sprite_flags;
        unsigned phase=(o->known&DOK_RUNTIME_CROP_PHASE)?(uint16_t)o->animation_a%18u:o->frame%18u;
        int du=((info->width[0]+1)/18)*(int)phase,dv=((info->height[0]+1)/18)*(int)phase;
        if(flags&0x440) {info->shift_x=(int16_t)-du;info->wrap=(flags&0x400)!=0;}
        else if(flags&0x880) {info->shift_x=(int16_t)du;info->wrap=(flags&0x800)!=0;}
        else if(flags&0x1100) {info->shift_y=(int16_t)-dv;info->wrap=(flags&0x1000)!=0;}
        else if(flags&0x2200) {info->shift_y=(int16_t)dv;info->wrap=(flags&0x2000)!=0;}
    }
#ifdef DOK_RENDER_PROCEDURAL_PROTOTYPE
    /* Two derived colors plus mortar, sampled from the source palette offline.
     * The procedural kernel uses the image pointer slots for these triplets. */
    for(unsigned p=0;p<2;++p) info->pixels[p]=world_procedural_colors+def->glyph*6+p*3;
#endif
    int found=0;
    for(unsigned i=0;i<world_ceiling_count;++i) if(world_ceiling[i]==ceiling) found=1;
    if(!found) {
        if(world_ceiling_count==16) return DOK_RENDER_CAPACITY;
        unsigned j=world_ceiling_count++;
        while(j && world_ceiling[j-1]<ceiling) {world_ceiling[j]=world_ceiling[j-1];--j;}
        world_ceiling[j]=ceiling;
    }
    return DOK_RENDER_OK;
}
static DOK_NOINLINE void world_plane_row(dok_renderer_workspace *w,const dok_runtime *r,
    const dok_projection_camera *c,dok_renderer_stats *stats,unsigned y,int height,unsigned plane,const int envelope[4]) {
    int dy=(int)y*10/9-c->center_y;
#ifdef DOK_RENDER_WORLD_SPANS_PROTOTYPE
    uint32_t setup_stamp=g_world_span_work[0]?render_ticks():0;
#endif
    if(!dy || (height-c->y<0)!=(dy<0)) return;
    /* Camera projection has depth+200. Keep that offset in the inverse lens. */
    int distance=WORLD_DIV(dy<0?(c->y-height)*200:(height-c->y)*200,dy<0?-dy:dy),depth=distance-200;
    if(depth<=0 || depth>6400) return;
    int32_t lateral=-distance*52101; /* Q16 approximation of159/200 */
    int32_t step=distance*65536/75;
    /* Form Q16 cell coordinates term by term. A whole-world Q16 sum can
     * exceed signed32 near the map edge before clipping, even though each
     * term and every cell-coordinate result are comfortably bounded. */
    int32_t wx=(c->x*65536)/480+((int32_t)(((int64_t)lateral*c->cosine)>>15))/480-(c->sine*depth*2)/480;
    int32_t wz=(c->z*65536)/480+((int32_t)(((int64_t)lateral*c->sine)>>15))/480+(c->cosine*depth*2)/480;
    int32_t dx=((int32_t)(((int64_t)step*c->cosine)>>15))/480;
    int32_t dz=((int32_t)(((int64_t)step*c->sine)>>15))/480;
    /* Carry cell coordinates and UV fractions together. This removes division
     * and modulo by480 from every sampled pixel. */
    unsigned row=light_index(depth);const uint8_t *lut=w->lights+row*256;
    unsigned mask=1u<<(y&31);uint32_t *bits=&coverage_bits[0][y>>5];
#ifdef DOK_RENDER_WORLD_SPANS_PROTOTYPE
    unsigned covered_mask=mask;
#ifdef DOK_RENDER_COARSE_PLANES_PROTOTYPE
    covered_mask|=mask<<1;
#endif
#endif
    const uint8_t *cells=r->maps+DOK_WORLD_FCMAP*4096;
#if defined(DOK_RENDER_PROCEDURAL_PROTOTYPE) || defined(DOK_RENDER_WORLD_SPANS_PROTOTYPE)
    int first=0,last=119;
    int origins[2]={wx,wz},steps[2]={dx,dz};
    for(unsigned axis=0;axis<2;++axis) {
        int lo=envelope[axis*2]*65536,hi=(envelope[axis*2+1]+1)*65536-1;
        int start=origins[axis],delta=steps[axis];
        if(!delta) {if(start<lo || start>hi) return;continue;}
        if(delta>0) {
            if(start<lo) {int n=WORLD_DIV(lo-start+delta-1,delta);if(n>first) first=n;}
            int end=WORLD_DIV(hi-start,delta);if(start>hi) return;if(end<last) last=end;
        } else {
            delta=-delta;
            if(start>hi) {int n=WORLD_DIV(start-hi+delta-1,delta);if(n>first) first=n;}
            int end=WORLD_DIV(start-lo,delta);if(start<lo) return;if(end<last) last=end;
        }
    }
    if(first>last || first>119 || last<0) return;
#ifdef DOK_RENDER_WORLD_SPANS_PROTOTYPE
    uint32_t sample_before=g_world_span_work[3],walk_stamp=0,covered_count=0;
    if(g_world_span_work[0]) {walk_stamp=render_ticks();g_world_span_work[1]+=walk_stamp-setup_stamp;++g_world_span_work[4];}
    wx+=first*dx;wz+=first*dz;bits+=first*5;
#ifdef DOK_RENDER_WORLD_LOOKUP_PROTOTYPE
    world_lookup_span span={w->frame+y*240+first*2,bits,cells,world_planes,
        wx,wz,dx,dz,mask,(unsigned)(last-first+1),plane,height,lut,dok_static_cells[static_view][0]};
    stats->pixels_written+=plane?dok_gba_world_lookup_ceiling(&span):dok_gba_world_lookup_floor(&span);
    (void)sample_before;(void)walk_stamp;(void)covered_count;(void)covered_mask;
#else
    for(int k=first;k<=last;) {
        unsigned skip=dok_gba_world_skip(bits,covered_mask,(unsigned)(last-k+1));
        k+=(int)skip;wx+=dx*(int)skip;wz+=dz*(int)skip;bits+=5*skip;covered_count+=skip;
        if(k>last) break;
        int cx=wx>>16,cz=wz>>16,n=last-k+1;
        if(dx>0) {int limit=WORLD_DIV((cx+1)*65536-1-wx,dx)+1;if(limit<n) n=limit;}
        else if(dx<0) {int limit=WORLD_DIV(wx-cx*65536,-dx)+1;if(limit<n) n=limit;}
        if(dz>0) {int limit=WORLD_DIV((cz+1)*65536-1-wz,dz)+1;if(limit<n) n=limit;}
        else if(dz<0) {int limit=WORLD_DIV(wz-cz*65536,-dz)+1;if(limit<n) n=limit;}
        unsigned cell=(unsigned)cz*64+(unsigned)cx;
        const world_plane_info *im=&world_planes[cells[cell]];
        if((im->ready&(1u<<plane)) && (!plane || im->ceiling==height) &&
           dok_static_cells[static_view][0][cell]!=2) {
            if(plane || (!im->shift_x && !im->shift_y)) {
                world_texture_span span={w->frame+y*240+k*2,bits,im->pixels[plane],lut,
                    wx,plane?wz:~wz,dx,plane?dz:-dz,mask,(unsigned)n,im->width[plane],im->height[plane],0,0};
                uint32_t stamp=g_world_span_work[0]?render_ticks():0;
                unsigned written=dok_gba_world_texture_span(&span);stats->pixels_written+=written;
                if(g_world_span_work[0]) {g_world_span_work[3]+=render_ticks()-stamp;++g_world_span_work[5];g_world_span_work[6]+=written;}
            } else {
#ifdef DOK_RENDER_WATER_SPANS_PROTOTYPE
                world_texture_span span={w->frame+y*240+k*2,bits,im->pixels[0],lut,
                    wx,~wz,dx,-dz,mask,(unsigned)n,im->width[0],im->height[0],im->shift_x,im->shift_y};
                stats->pixels_written+=im->wrap?dok_gba_world_texture_span_wrap(&span):dok_gba_world_texture_span_shift(&span);
#else
                /* Animated crop/wrap is exceptional; retain its source phase. */
                int32_t u=wx,v=wz;uint32_t *b=bits;
                for(int j=0;j<n;++j,u+=dx,v+=dz,b+=5) if(!(*b&mask)) {
                    int tx=(int)(((uint32_t)(u&65535)*im->width[0])>>16)+im->shift_x;
                    int ty=(int)(((uint32_t)(65535-(v&65535))*im->height[0])>>16)+im->shift_y;
                    if(im->wrap) {
                        if(tx<0) tx+=im->width[0]+1;else if(tx>im->width[0]) tx-=im->width[0]+1;
                        if(ty<0) ty+=im->height[0]+1;else if(ty>im->height[0]) ty-=im->height[0]+1;
                    }
                    unsigned texel=(tx<0 || tx>im->width[0] || ty<0 || ty>im->height[0])?0:
                        im->pixels[0][(unsigned)ty*(im->width[0]+1)+(unsigned)tx];
                    store_pixel(w->frame+y*240+(k+j)*2,lut[texel]);*b|=mask;++stats->pixels_written;
                    if(g_world_span_work[0])++g_world_span_work[8];
                }
#endif
            }
        }
        k+=n;wx+=dx*n;wz+=dz*n;bits+=5*n;
    }
    if(g_world_span_work[0]) {g_world_span_work[2]+=render_ticks()-walk_stamp-(g_world_span_work[3]-sample_before);g_world_span_work[7]+=covered_count;}
#endif
#else
    procedural_span span={w->frame+y*240+first*2,bits+first*5,cells,world_planes,
        wx+first*dx,wz+first*dz,dx,dz,mask,(unsigned)(last-first+1),plane,height,lut};
    stats->pixels_written+=dok_gba_procedural_span(&span);
#endif
    return;
#else
    for(unsigned x=0;x<240;x+=2,wx+=dx,wz+=dz,bits+=5) {
        if(*bits&mask) continue;
        int cx=wx>>16,cz=wz>>16;
        if(cx<envelope[0] || cx>envelope[1] || cz<envelope[2] || cz>envelope[3]) continue;
        unsigned cell=(unsigned)cz*64+(unsigned)cx;
        const world_plane_info *im=&world_planes[cells[cell]];
        if(!(im->ready&(1u<<plane)) || (plane && im->ceiling!=height)) continue;
#ifdef DOK_RENDER_PROCEDURAL_PROTOTYPE
        /* World-anchored quarter-cell tiles; two tones and a mortar line. */
        unsigned choice=((unsigned)(wx^wz)>>14)&1;
        if(((unsigned)wx&16383)<512 || ((unsigned)wz&16383)<512) choice=2;
        unsigned texel=world_procedural_colors[cells[cell]*6+plane*3+choice];
#else
        int tx=(int)(((uint32_t)(wx&65535)*im->width[plane])>>16);
        unsigned fy=(unsigned)wz&65535;
        int ty=(int)(((plane?fy:65535-fy)*im->height[plane])>>16);
        if(!plane) {tx+=im->shift_x;ty+=im->shift_y;
            if(im->wrap) {
                if(tx<0) tx+=im->width[0]+1;else if(tx>im->width[0]) tx-=im->width[0]+1;
                if(ty<0) ty+=im->height[0]+1;else if(ty>im->height[0]) ty-=im->height[0]+1;
            }
        }
        unsigned texel=(tx<0 || tx>im->width[plane] || ty<0 || ty>im->height[plane])?0:
            im->pixels[plane][(unsigned)ty*(im->width[plane]+1)+(unsigned)tx];
#endif
        store_pixel(w->frame+y*240+x,lut[texel]);
        *bits|=mask;COVER_COUNT(w,x)|=128;++stats->pixels_written;
    }
#endif
}
#ifdef DOK_RENDER_WORLD_SPANS_PROTOTYPE
static DOK_NOINLINE void world_plane_draw(dok_renderer_workspace *w,const dok_runtime *r,
#else
static DOK_COLD void world_plane_draw(dok_renderer_workspace *w,const dok_runtime *r,
#endif
    const dok_projection_camera *c,dok_renderer_stats *stats) {
#ifdef DOK_RENDER_WORLD_SPANS_PROTOTYPE
    for(unsigned k=1;k<9;++k) g_world_span_work[k]=0;
#endif
#ifdef DOK_RENDER_RAY_WALLS_PROTOTYPE
    const int *envelope=world_shared_envelope;
#else
    int envelope[4]={63,0,63,0};const int lateral[4]={-1,1,-6,6},depth[4]={-2,-2,7,7};
    for(unsigned k=0;k<4;++k) {
        int dx=sar((lateral[k]*c->cosine-depth[k]*c->sine)*480,15);
        int dz=sar((lateral[k]*c->sine+depth[k]*c->cosine)*480,15);
        int x=clamp((c->x+dx)/480,0,63),z=clamp((c->z+dz)/480,0,63);
        if(x<envelope[0]) envelope[0]=x;if(x>envelope[1]) envelope[1]=x;
        if(z<envelope[2]) envelope[2]=z;if(z>envelope[3]) envelope[3]=z;
    }
#endif
    for(unsigned y=0;y<144;
#ifdef DOK_RENDER_COARSE_PLANES_PROTOTYPE
        y+=2
#else
        ++y
#endif
    ) {
        if((int)y*10/9>c->center_y) world_plane_row(w,r,c,stats,y,0,0,envelope);
        else for(unsigned i=0;i<world_ceiling_count;++i) world_plane_row(w,r,c,stats,y,world_ceiling[i],1,envelope);
    }
#if defined(DOK_RENDER_PROCEDURAL_PROTOTYPE) || defined(DOK_RENDER_WORLD_SPANS_PROTOTYPE)
    for(unsigned x=0;x<240;x+=2) COVER_COUNT(w,x)|=128;
#endif
}
