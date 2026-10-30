#ifndef DOK_FRAME_STORE_H
#define DOK_FRAME_STORE_H
#include "dok/texture_cache.h"
#include "dok/sprite_assets.h"
typedef struct dok_frame_record {uint32_t id,offset,bytes,decoded_bytes;} dok_frame_record;
typedef struct dok_frame_store {
    dok_frame_record records[DOK_TEXTURE_CACHE_SLOTS];
    const unsigned char *pool;
    uint32_t bytes,count,decoded_bytes,decodes,decoded_total;
} dok_frame_store;
dok_status dok_frame_pack(const unsigned char *,uint32_t,unsigned char *,uint32_t,uint32_t *);
dok_status dok_frame_unpack(const unsigned char *,uint32_t,unsigned char *,uint32_t);
/* Every original payload goes through N04 SHA/schema validation at loading.
 * Store and raw cache partition the existing1MiB asset slab; no hidden heap. */
dok_status dok_frame_store_load(dok_frame_store *,const dok_sprite_asset_bank *,const dok_asset_session *,unsigned char *,uint32_t,unsigned char *,uint32_t);
dok_status dok_frame_store_require(dok_frame_store *,dok_texture_cache *,const dok_asset_entry *);
typedef struct dok_frame_batch_workspace {
    dok_texture_cache planned;
    uint32_t original_offsets[DOK_TEXTURE_CACHE_SLOTS];
    const dok_frame_record *sources[DOK_TEXTURE_CACHE_SLOTS];
    uint16_t order[DOK_TEXTURE_CACHE_SLOTS];
} dok_frame_batch_workspace;
/* Exact ordered require sequence, including duplicate hits and failure-prefix
 * mutations. Workspace must be aligned and disjoint from store/cache/payloads
 * and the entries array. It is borrowed only during this call.
 * Count must not exceed DOK_TEXTURE_CACHE_SLOTS. */
dok_status dok_frame_store_require_many(dok_frame_store *,dok_texture_cache *,
    const dok_asset_entry *const *,uint32_t,dok_frame_batch_workspace *);
#endif
