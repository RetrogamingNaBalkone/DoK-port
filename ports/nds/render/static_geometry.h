#ifndef DOK_NDS_STATIC_GEOMETRY_H
#define DOK_NDS_STATIC_GEOMETRY_H
#include "dok/geometry.h"
/* One loaded 64x64 game level, no refill or invalidation protocol. Current
 * pickup/corpse writers preserve geometry; doors crop afterwards. Backing is
 * charged to the existing assets slab before the raw frame cache. */
int dok_static_geometry_load(const dok_initial_world *,const dok_geometry_binding [256],
    void *,uint32_t capacity,uint32_t *used);
void dok_static_geometry_reset(void);
int dok_static_geometry_bound(const dok_initial_world *,const dok_geometry_binding [256]);
int dok_static_geometry_read(const dok_initial_world *,const dok_geometry_binding [256],
    uint32_t x,uint32_t z,dok_geometry_cell *);
/* Vertices are cell-relative X/Z, absolute Y, and whole-texel U/V. */
typedef struct dok_static_quad {
    int16_t vertex[4][5];
    uint16_t image_flags,object_flags;
    uint8_t material,orientation,kind,glyph;
} dok_static_quad;
/* Borrow a cell's source-order template while the level remains bound. */
const dok_static_quad *dok_static_geometry_span(const dok_initial_world *,
    const dok_geometry_binding [256],uint32_t x,uint32_t z,uint32_t *count);
void dok_static_geometry_unpack(const dok_static_quad *,uint32_t x,uint32_t z,
    dok_geometry_quad *);
/* Depth (relative to the near plane) at which every level vertex is inside
 * each vertical plane. Computed once per camera. */
void dok_static_geometry_depth_limits(int32_t camera_y,int64_t limits[2]);
/* Returns remaining plane bits, or 32 for full rejection. A non-null span
 * borrows the packed source records in original order, including overhangs. */
uint32_t dok_static_geometry_cull(const dok_initial_world *,const dok_geometry_binding [256],
    uint32_t x,uint32_t z,const int64_t origin[3],
    const int64_t minimum[3],const int64_t maximum[3],const int64_t limits[2],
    const dok_static_quad **span,uint32_t *count);
typedef struct dok_static_map_shape {
    int32_t ax,az,bx,bz;
    uint16_t flags;
    uint8_t glyph,kind;
} dok_static_map_shape;
int dok_static_geometry_shapes(const dok_initial_world *,const dok_geometry_binding [256],
    uint32_t x,uint32_t z,dok_static_map_shape [DOK_GEOMETRY_CELL_MAX],uint32_t *count);
#endif
