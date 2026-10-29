#ifndef DOK_NDS_EFFECT_CACHE_H
#define DOK_NDS_EFFECT_CACHE_H
#include "dok/effects.h"
#define DOK_EFFECT_CACHE_SLOTS 128u
#define DOK_EFFECT_CACHE_BYTES (384u*1024u)
typedef struct dok_effect_cache_record {
    uint64_t key;
    uint32_t offset,bytes,age,pinned;
} dok_effect_cache_record;
typedef struct dok_effect_cache {
    unsigned char *pool;
    uint32_t capacity,used,age,count,hits,misses,evictions,generated_bytes;
    dok_effect_cache_record records[DOK_EFFECT_CACHE_SLOTS];
} dok_effect_cache;
void dok_effect_cache_init(dok_effect_cache *cache,unsigned char *pool,uint32_t capacity);
dok_status dok_effect_cache_begin(dok_effect_cache *cache);
/* Complete frame pins, followed by pointer resolution after all requires.
 * key encodes the source image, transparency, crop phase/direction and zero
 * mask row. Palette color alone never duplicates texels. */
dok_status dok_effect_cache_require(dok_effect_cache *cache,uint64_t key,
    const unsigned char *source,uint16_t width,uint16_t height,uint16_t sw,uint16_t sh,
    uint16_t flags,uint16_t phase,const unsigned char *light_row,uint32_t masked);
const unsigned char *dok_effect_cache_pixels(const dok_effect_cache *cache,uint64_t key);
#endif
