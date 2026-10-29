#ifndef DOK_GEOMETRY_CACHE_H
#define DOK_GEOMETRY_CACHE_H
#include "dok/geometry.h"
/* Bounded translation-invariant pattern cache. No final camera/door/AI data. */
void dok_geometry_cache_reset(void);
void dok_geometry_cache_begin(const dok_initial_world *,const dok_geometry_binding [256]);
void dok_geometry_cache_stats(uint32_t *hits,uint32_t *misses,uint32_t *bytes);
dok_geometry_status dok_geometry_build_cell_uncached(const dok_initial_world *,const dok_geometry_binding [256],uint32_t,uint32_t,dok_geometry_cell *,dok_geometry_cell *);
#endif
