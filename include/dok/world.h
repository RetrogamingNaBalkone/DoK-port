#ifndef DOK_WORLD_H
#define DOK_WORLD_H
#include "dok/level.h"
#include "dok/res.h"

#define DOK_WORLD_STATE_BYTES 248
/* Partial native scalar state, with explicit per-byte validity. Unknown bytes
 * are zero storage, NOT known native zeroes. Never serialize this as a DOS save.
 * Resource pointers/vtables/allocator-dependent links remain unknown.
 */
typedef struct {
    uint8_t bytes[DOK_WORLD_STATE_BYTES], known[DOK_WORLD_STATE_BYTES/8];
    uint16_t native_size, placements;
} dok_world_object;
typedef enum { DOK_WORLD_MMAP, DOK_WORLD_TMAP, DOK_WORLD_FCMAP, DOK_WORLD_VISITED } dok_world_plane;
typedef struct {
    const dok_level *level;
    dok_world_object *objects; /* Same indices as level.objects: one per glyph. */
    uint8_t *maps; /* Four packed planes; world row 0 = last source row. */
    size_t cells;
    uint16_t player_index; /* object index + 1, zero if no placed player */
    int16_t pickup_99_total; /* Native signed word, wraps modulo 65536. */
} dok_world;
typedef enum {
    DOK_WORLD_OK, DOK_WORLD_ARGUMENT, DOK_WORLD_PROFILE, DOK_WORLD_CAPACITY,
    DOK_WORLD_DEFINITION, DOK_WORLD_RESOURCE, DOK_WORLD_UNDEFINED_GLYPH
} dok_world_status;
typedef struct { dok_world_status status; uint16_t object_index; uint16_t cell; uint8_t plane, glyph; } dok_world_error;

/* Takes a successful dok_level_parse result; all input/output buffers must be
 * distinct and stay alive. Square maps 1..64 only: original row flip uses width
 * as its last row index. No allocation. Failure may partially write output.
 * archive can be NULL only if no object needs animation frame metadata.
 * This builds verified startup scalars/maps, not an event loop or full C++ heap.
 */
dok_world_status dok_world_build(dok_world *out,const dok_level *level,
    const dok_res_archive *archive,dok_world_object *objects,size_t object_capacity,
    uint8_t *maps,size_t map_capacity,dok_world_error *error);
/* Read a verified 1/2/4-byte native scalar as bits or as a signed integer.
 * On unknown/out-of-range data return 0 and leave output untouched.
 */
int dok_world_read_u(const dok_world_object *object,size_t offset,unsigned width,uint32_t *out);
int dok_world_read_s(const dok_world_object *object,size_t offset,unsigned width,int32_t *out);
#endif
