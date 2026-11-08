#ifndef DOK_NDS_STATIC_GEOMETRY_H
#define DOK_NDS_STATIC_GEOMETRY_H
#include "dok/geometry.h"
/* One loaded 64x64 game level, no refill or invalidation protocol. Current
 * pickup/corpse writers preserve geometry; doors crop afterwards. Backing is
 * charged to the existing assets slab before the raw frame cache. */
int dok_static_geometry_load(const dok_initial_world *,const dok_geometry_binding [256],
    void *,uint32_t capacity,uint32_t *used);
void dok_static_geometry_reset(void);
int dok_static_geometry_read(const dok_initial_world *,const dok_geometry_binding [256],
    uint32_t x,uint32_t z,dok_geometry_cell *);
/* Exact horizontal-plane admission for patterns proved inside their cell.
 * Returns active plane bits (vertical bits retained), or32 for full rejection.
 * Unbound/overhanging patterns retain all31 bits and the ordinary quad path. */
uint32_t dok_static_geometry_cull(const dok_initial_world *,const dok_geometry_binding [256],
    uint32_t x,uint32_t z,const int64_t origin[3],
    const int64_t minimum[3],const int64_t maximum[3],uint32_t *count);
typedef struct dok_static_map_shape {
    int32_t ax,az,bx,bz;
    uint16_t flags;
    uint8_t glyph,kind;
} dok_static_map_shape;
int dok_static_geometry_shapes(const dok_initial_world *,const dok_geometry_binding [256],
    uint32_t x,uint32_t z,dok_static_map_shape [DOK_GEOMETRY_CELL_MAX],uint32_t *count);
#endif
