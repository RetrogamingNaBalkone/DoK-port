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
#endif
