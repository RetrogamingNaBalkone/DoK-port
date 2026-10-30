#ifndef DOK_NDS_GEOMETRY_PRIVATE_H
#define DOK_NDS_GEOMETRY_PRIVATE_H
#include "dok/geometry.h"
#include "dok/geometry_prepared.h"
/* Scene-only transient output. On success count/reserved and active quads are
 * complete; inactive tail is unspecified. On failure the caller ignores all
 * cell bytes. No allocation, retained state or public out/error contract change. */
dok_geometry_status dok_geometry_build_cell_inplace(const dok_initial_world *,
    const dok_geometry_binding [256],uint32_t,uint32_t,dok_geometry_cell *);
dok_geometry_status dok_geometry_build_cell_prepared(const dok_initial_world *,
    const dok_geometry_binding [256],uint32_t,uint32_t,dok_geometry_cell *,
    const dok_geometry_field54 [DOK_LEVEL_MAX_OBJECTS]);
#endif
