#include "dok/geometry_cache.h"
#include <string.h>
#ifndef GAMEPLAY_GEOMETRY_CACHE
#define GAMEPLAY_GEOMETRY_CACHE 1
#endif
#if GAMEPLAY_GEOMETRY_CACHE
#define SLOTS 64u
typedef struct Pattern {
    dok_geometry_cell cell;
    unsigned char key[10];
    uint16_t valid;
    int32_t x,z;
    uint32_t age;
} Pattern;
static struct {
    Pattern patterns[SLOTS];
    dok_geometry_binding bindings[256];
    const dok_initial_world *world;
    const dok_level_definition *definition;
    uint32_t epoch,age,hits,misses,enabled;
} cache;
void dok_geometry_cache_reset(void){memset(&cache,0,sizeof(cache));}
void dok_geometry_cache_begin(const dok_initial_world *w,const dok_geometry_binding b[256])
{
    if(!w||!w->definition||!b){cache.enabled=0;return;}
    if(cache.world!=w || cache.definition!=w->definition || cache.epoch!=w->epoch || memcmp(cache.bindings,b,sizeof(cache.bindings))) {
        dok_geometry_cache_reset();cache.world=w;cache.definition=w->definition;cache.epoch=w->epoch;memcpy(cache.bindings,b,sizeof(cache.bindings));
    }
    cache.enabled=1;
}
void dok_geometry_cache_stats(uint32_t *h,uint32_t *m,uint32_t *b)
{if(h)*h=cache.hits;if(m)*m=cache.misses;if(b)*b=sizeof(cache);}
dok_geometry_status dok_geometry_build_cell(const dok_initial_world *w,const dok_geometry_binding b[256],uint32_t x,uint32_t z,dok_geometry_cell *candidate,dok_geometry_cell *out)
{
    if(!cache.enabled || w!=cache.world || b==NULL || !candidate || !out || candidate==out || !w || !w->definition ||
       w->definition->settings[0]<=0 || w->definition->settings[2]<=0 || x>=(uint32_t)w->definition->settings[0] || z>=(uint32_t)w->definition->settings[2])
        return dok_geometry_build_cell_uncached(w,b,x,z,candidate,out);
    uint32_t width=(uint32_t)w->definition->settings[0],length=(uint32_t)w->definition->settings[2];
    uint32_t nx[5]={x,x,x?x-1u:0u,x,x+1u<width?x+1u:x};
    uint32_t nz[5]={z,z?z-1u:0u,z,z+1u<length?z+1u:z,z};
    unsigned char key[10];uint32_t hash=2166136261u;
    for(uint32_t i=0;i<5u;++i){uint32_t n=nz[i]*width+nx[i];key[i*2]=w->maps[DOK_WORLD_TMAP][n];key[i*2+1]=w->maps[DOK_WORLD_FCMAP][n];hash=(hash^key[i*2])*16777619u;hash=(hash^key[i*2+1])*16777619u;}
    uint32_t victim=(hash^(hash>>16u))&(SLOTS-1u);
    {Pattern *p=&cache.patterns[victim];
        if(p->valid && !memcmp(p->key,key,sizeof(key))) {
            *out=p->cell;int32_t dx=((int32_t)x-p->x)*w->definition->settings[3],dz=((int32_t)z-p->z)*w->definition->settings[3];
            for(uint32_t q=0;q<out->count;++q){out->quads[q].cell_x=(uint8_t)x;out->quads[q].cell_z=(uint8_t)z;
                for(uint32_t v=0;v<4u;++v){out->quads[q].vertices[v].x+=dx;out->quads[q].vertices[v].z+=dz;}}
            p->age=++cache.age;++cache.hits;return DOK_GEOMETRY_OK;
        }
    }
    dok_geometry_status s=dok_geometry_build_cell_uncached(w,b,x,z,candidate,out);++cache.misses;
    if(s==DOK_GEOMETRY_OK){Pattern *p=&cache.patterns[victim];p->cell=*out;memcpy(p->key,key,sizeof(key));p->x=(int32_t)x;p->z=(int32_t)z;p->valid=1;p->age=++cache.age;}
    return s;
}
#else
void dok_geometry_cache_reset(void){}
void dok_geometry_cache_begin(const dok_initial_world *w,const dok_geometry_binding b[256]){(void)w;(void)b;}
void dok_geometry_cache_stats(uint32_t *h,uint32_t *m,uint32_t *b){if(h)*h=0;if(m)*m=0;if(b)*b=0;}
dok_geometry_status dok_geometry_build_cell(const dok_initial_world *w,const dok_geometry_binding b[256],uint32_t x,uint32_t z,dok_geometry_cell *c,dok_geometry_cell *o)
{return dok_geometry_build_cell_uncached(w,b,x,z,c,o);}
#endif
