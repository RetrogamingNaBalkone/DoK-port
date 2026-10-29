#include "dok/effect_cache.h"
#include <stdio.h>
#include <string.h>
int main(void)
{
    dok_effect_cache cache;unsigned char pool[128],a[64],b[64],c[64],light[256];
    unsigned checks=0,failures=0;
#define CHECK(x) do{++checks;if(!(x))++failures;}while(0)
    memset(a,1,64);memset(b,2,64);memset(c,3,64);
    for(unsigned i=0;i<256;++i)light[i]=(unsigned char)i;
    dok_effect_cache_init(&cache,pool,sizeof(pool));
    CHECK(dok_effect_cache_begin(&cache)==DOK_STATUS_OK);
    CHECK(dok_effect_cache_require(&cache,1,a,8,8,8,8,0,0,light,0)==DOK_STATUS_OK);
    CHECK(dok_effect_cache_require(&cache,2,b,8,8,8,8,0,0,light,0)==DOK_STATUS_OK);
    CHECK(dok_effect_cache_require(&cache,3,c,8,8,8,8,0,0,light,0)==DOK_STATUS_NO_SPACE);
    CHECK(dok_effect_cache_begin(&cache)==DOK_STATUS_OK);
    CHECK(dok_effect_cache_require(&cache,1,a,8,8,8,8,0,0,light,0)==DOK_STATUS_OK);
    const unsigned char *pinned=dok_effect_cache_pixels(&cache,1);
    CHECK(dok_effect_cache_require(&cache,3,c,8,8,8,8,0,0,light,0)==DOK_STATUS_OK);
    CHECK(cache.used==128 && cache.count==2 && cache.evictions==1);
    CHECK(pinned==dok_effect_cache_pixels(&cache,1) && !memcmp(pinned,a,64));
    CHECK(!dok_effect_cache_pixels(&cache,2));
    CHECK(!memcmp(dok_effect_cache_pixels(&cache,3),c,64));
    CHECK(dok_effect_cache_require(&cache,2,b,8,8,8,8,0,0,light,0)==DOK_STATUS_NO_SPACE);
    printf("{\"checks\":%u,\"failures\":%u}\n",checks,failures);
    return failures?1:0;
}
