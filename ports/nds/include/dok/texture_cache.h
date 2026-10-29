#ifndef DOK_TEXTURE_CACHE_H
#define DOK_TEXTURE_CACHE_H
#include "dok/assets.h"
#define DOK_TEXTURE_CACHE_SLOTS 256u
typedef struct dok_texture_cache_record {
    uint64_t key;
    uint32_t offset, bytes, age, pinned;
} dok_texture_cache_record;
typedef struct dok_texture_cache {
    unsigned char *pool;
    uint32_t capacity, used, age, count, hits, misses, evictions, read_bytes;
    dok_texture_cache_record records[DOK_TEXTURE_CACHE_SLOTS];
} dok_texture_cache;
void dok_texture_cache_init(dok_texture_cache *cache,unsigned char *pool,uint32_t capacity);
/* All borrowers from the previous frame must be released before begin.
 * Require may compact/move records. Obtain pointers only AFTER all requires.
 * Every requested record stays pinned until the next begin; never evict a
 * current-frame texture. Errors retain valid records; no partial new entry. */
dok_status dok_texture_cache_begin(dok_texture_cache *cache);
dok_status dok_texture_cache_require(dok_texture_cache *cache,
    const dok_asset_session *session,const dok_asset_entry *entry);
const unsigned char *dok_texture_cache_pixels(const dok_texture_cache *cache,
    dok_asset_id id);
#endif
