/* Software base planes retain the HW backend's32x32 source art, animated
 * water phases, LIGHT0/7/15 and3600 depth. CPU scene is packed120x128. */
typedef struct {uint8_t *dst;const uint8_t *const *materials;const uint8_t *cells;
    const uint32_t *coverage;int wx,wz,dx,dz;unsigned bit,count,black;int x0,z0,x1,z1;} sector_bg_span;
extern unsigned dok_gba_bitmap_background(const sector_bg_span *);
extern const uint8_t dok_sector_material_cells[6][2][4096];
extern const uint8_t dok_hw_bg_bank[],dok_hw_bg_counts[6],dok_hw_bg_animated[6][8];
extern const uint16_t dok_hw_bg_objects[6][8];
extern const uint32_t dok_hw_bg_offsets[6][8][18][3];
static DOK_COLD void sector_bitmap_background(dok_renderer_workspace *w,const dok_runtime *r) {
    const uint8_t *materials[3][8];
    for(unsigned m=0;m<8;++m) {
        unsigned phase=0;
        if(m<dok_hw_bg_counts[static_view] && dok_hw_bg_animated[static_view][m]) {
            const dok_runtime_object *o=&r->objects[dok_hw_bg_objects[static_view][m]];
            phase=(o->known&DOK_RUNTIME_CROP_PHASE)?(uint16_t)o->animation_a%18u:o->frame%18u;
        }
        for(unsigned band=0;band<3;++band)materials[band][m]=dok_hw_bg_bank+dok_hw_bg_offsets[static_view][m][phase][band];
    }
    const dok_projection_camera *c=sector_camera;
    for(unsigned y=0;y<128;++y) {
        int dy=(int)y*5/4-79,height=dy<0?-951:0;
        int z=dy?(height-c->y)*200/dy-200:-1;
        if(z<=0 || z>3600)continue;
        int distance=z+200,lateral=-distance*52101,step=distance*65536/75;
        int wx=c->x*65536/480+(int)(((int64_t)lateral*c->cosine)>>15)/480-c->sine*z*2/480;
        int wz=c->z*65536/480+(int)(((int64_t)lateral*c->sine)>>15)/480+c->cosine*z*2/480;
        int dx=(int)(((int64_t)step*c->cosine)>>15)/480,dz=(int)(((int64_t)step*c->sine)>>15)/480;
        unsigned light=light_index(z),band=light<4?0:light<11?1:2;
        sector_bg_span span={scene_pointer(w->frame,0,y),materials[band],dok_sector_material_cells[static_view][dy<0],
            &coverage_bits[0][y>>5],wx,wz,dx,dz,1u<<(y&31),120,g_hw_tile_black,
            g_hw_bg_envelope[0],g_hw_bg_envelope[2],g_hw_bg_envelope[1],g_hw_bg_envelope[3]};
        dok_gba_bitmap_background(&span);
    }
}
