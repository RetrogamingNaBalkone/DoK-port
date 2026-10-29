#ifndef DOK_NDS_GEOMETRY_H
#define DOK_NDS_GEOMETRY_H
#include "dok/world.h"
#include <stdint.h>

#define DOK_GEOMETRY_CELL_MAX 12u
enum { DOK_GEOMETRY_IMAGE=0, DOK_GEOMETRY_FLOOR=1,
       DOK_GEOMETRY_CEILING=2, DOK_GEOMETRY_CEILING_SIDE=3,
       DOK_GEOMETRY_BINDING_SLOTS=4 };
typedef struct dok_geometry_image {
    uint16_t material, width, height, flags;
} dok_geometry_image;
typedef struct dok_geometry_binding {
    dok_geometry_image images[DOK_GEOMETRY_BINDING_SLOTS];
} dok_geometry_binding;
typedef struct dok_geometry_vertex {
    int32_t x, y, z, u_q16, v_q16;
} dok_geometry_vertex;
typedef enum dok_geometry_kind {
    DOK_GEOMETRY_WALL=0, DOK_GEOMETRY_DOOR=1,
    DOK_GEOMETRY_FLOOR_QUAD=2, DOK_GEOMETRY_CEILING_QUAD=3,
    DOK_GEOMETRY_CEILING_BAND=4
} dok_geometry_kind;
typedef struct dok_geometry_quad {
    dok_geometry_vertex vertices[4];
    uint16_t material, image_flags, object_flags, orientation;
    uint8_t kind, cell_x, cell_z, source_glyph;
} dok_geometry_quad;
typedef struct dok_geometry_cell {
    dok_geometry_quad quads[DOK_GEOMETRY_CELL_MAX];
    uint16_t count, reserved;
} dok_geometry_cell;
typedef enum dok_geometry_status {
    DOK_GEOMETRY_OK=0, DOK_GEOMETRY_INVALID_ARGUMENT=1,
    DOK_GEOMETRY_MISSING_BINDING=2, DOK_GEOMETRY_UNSUPPORTED=3,
    DOK_GEOMETRY_LIMIT=4
} dok_geometry_status;

/* Pure original-profile cell builder, not camera clipping or GPU emission.
 * Bindings use glyph indexing and resolve current INITIAL images. IMAGE is
 * TMap Sprite; FC FLOOR must already apply class12 current Sprite getter.
 * Class13 retains the ordinary native+5A floor image getter.
 * Image flags are runtime image flags, not object flags. Zero material/size
 * means unavailable. The caller supplies a separate transient candidate;
 * candidate may change on error, out stays unchanged. No allocation/SDK/I/O.
 * World and borrowed definitions/bindings must remain live and immutable. */
dok_geometry_status dok_geometry_build_cell(const dok_initial_world *world,
    const dok_geometry_binding bindings[256], uint32_t x, uint32_t z,
    dok_geometry_cell *candidate, dok_geometry_cell *out);
#endif
