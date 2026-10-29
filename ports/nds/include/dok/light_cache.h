#ifndef DOK_NDS_LIGHT_CACHE_H
#define DOK_NDS_LIGHT_CACHE_H
#include "dok/effects.h"
#define DOK_LIGHT_CACHE_INPUTS 384u
/* Lossless integer packing; no UV precision or source-ID reduction. */
typedef struct dok_light_cache_quad {
    uint32_t attributes,source_id;
    int16_t origin[3],xyz[4][3];
    int32_t uv[4][2];
} dok_light_cache_quad;
_Static_assert(sizeof(dok_light_cache_quad)==72u,"portable exact packet packing");
typedef struct dok_light_cache {
    uint32_t valid,input_count,output_count,epoch,queries,hits,misses,stores,last_hit,unsupported;
    dok_nds_render_camera camera;
    int16_t near_depth;
    uint16_t reserved;
    dok_light_counts counts;
    dok_light_cache_quad input[DOK_LIGHT_CACHE_INPUTS];
    dok_light_cache_quad output[DOK_RENDER_QUADS];
} dok_light_cache;
_Static_assert(sizeof(dok_light_cache)<180u*1024u,"N13P bounded static-cache experiment");
void dok_light_cache_reset(dok_light_cache *cache);
/* Exact input comparison: no probabilistic hash, pointer borrowing or hidden
 * dependency on simulation time, pixels or palette values. Input includes
 * camera/near/epoch, all source vertices, UVs, slots, IDs and origins. A hit
 * preserves the current scene lease/generation; otherwise native N13 apply
 * runs normally. Unsupported packing is a measured miss, never a lost face.
 * Caller resets at every level/presentation binding change. */
dok_render_scene_result dok_light_cache_apply(dok_light_cache *cache,
    dok_render_scene *scene,int16_t near_depth,dok_light_workspace *workspace,dok_light_counts *out);
#endif
