/* PERF06 visual experiment. Three cells retain their original detail.
 * Never changes geometry leases, runtime maps, collision or object lists. */
#define PERF06_FAR 1440
extern unsigned dok_gba_flat_column(uint8_t *,uint32_t *,unsigned,unsigned,unsigned,unsigned);
extern unsigned dok_gba_flat_span(uint8_t *,uint32_t *,unsigned,unsigned,unsigned);
#ifdef DOK_RENDER_HW_FAR_FLAT_PROTOTYPE
static DOK_COLD unsigned far_flat_column(dok_renderer_workspace *w,unsigned x,int lo,int hi,unsigned color) {
    if(lo>hi)return 0;
    if(!color)color=g_hw_tile_black;
    unsigned n=dok_gba_flat_column(scene_pointer(w->frame,x,(unsigned)lo),
        &coverage_bits[x>>1][(unsigned)lo>>5],1u<<((unsigned)lo&31),
        (unsigned)(hi-lo+1),color,(unsigned)lo&7);
    COVER_COUNT(w,x)|=128;return n;
}
#endif
#ifdef DOK_RENDER_HW_FAR_FENCES_PROTOTYPE
extern const uint32_t dok_perf06_fence_offsets[1536];
extern const uint8_t dok_perf06_fences[];
extern unsigned dok_gba_far_fence(const void *);
#endif
