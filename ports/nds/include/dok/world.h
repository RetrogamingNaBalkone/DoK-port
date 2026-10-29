#ifndef DOK_NDS_WORLD_H
#define DOK_NDS_WORLD_H
#include "dok/assets.h"
#include <stdint.h>

#define DOK_LEVEL_MAX_OBJECTS 256u
#define DOK_LEVEL_MAX_FIELDS 19u
#define DOK_LEVEL_MAX_SIDE 64u
#define DOK_LEVEL_MAX_CELLS 4096u
#define DOK_LEVEL_MAX_LINE 178u

typedef enum dok_world_status {
    DOK_WORLD_OK=0, DOK_WORLD_INVALID_ARGUMENT=1,
    DOK_WORLD_MALFORMED=2, DOK_WORLD_UNSUPPORTED=3,
    DOK_WORLD_LIMIT=4, DOK_WORLD_DUPLICATE=5,
    DOK_WORLD_MISSING_GLYPH=6, DOK_WORLD_MAP_MISMATCH=7
} dok_world_status;

typedef struct dok_level_span { uint32_t offset, length; } dok_level_span;
typedef enum dok_level_field_kind {
    DOK_LEVEL_WORD=0, DOK_LEVEL_LONG=1, DOK_LEVEL_CHARACTER=2,
    DOK_LEVEL_IMAGE=3, DOK_LEVEL_ANIMATION=4,
    DOK_LEVEL_SOUND=5, DOK_LEVEL_TEXT=6
} dok_level_field_kind;

/* Offset identifies a documented original native field, never disk layout.
 * token retains the complete original bytes; TEXT consumers use at most 39
 * bytes to match the original getline buffer. value is zero for strings. */
typedef struct dok_level_field {
    uint16_t native_offset;
    uint8_t kind, reserved;
    int32_t value;
    dok_level_span token;
} dok_level_field;
typedef struct dok_level_object {
    uint16_t class_id, flags;
    int16_t width_parameter, height_parameter, sprite_flags;
    uint8_t glyph, marker;
    uint16_t field_count, reserved;
    dok_level_span sprite, animation, sound, raw_line, raw_tail;
    dok_level_field fields[DOK_LEVEL_MAX_FIELDS];
} dok_level_object;

enum {
    DOK_LEVEL_WIDTH=0, DOK_LEVEL_HEIGHT=1, DOK_LEVEL_LENGTH=2,
    DOK_LEVEL_CELL_SIDE=3, DOK_LEVEL_CELL_HEIGHT=4,
    DOK_LEVEL_FLOOR_COLOR=5, DOK_LEVEL_CEILING_COLOR=6,
    DOK_LEVEL_MIN_DISTANCE=7,
    DOK_LEVEL_TMAP=0, DOK_LEVEL_MMAP=1, DOK_LEVEL_FCMAP=2,
    DOK_WORLD_MMAP=0, DOK_WORLD_TMAP=1, DOK_WORLD_FCMAP=2,
    DOK_WORLD_VISITED=3
};
/* Immutable parsed data, borrowing the complete CP866 asset. Keep its lease
 * pinned through every borrower. All unused array elements are zero.
 * source bytes, resource strings and numeric token spans are never rewritten.
 * An effective profile copy changes numeric values, not source provenance. */
typedef struct dok_level_definition {
    const unsigned char *source;
    uint32_t source_bytes, source_asset_id;
    uint16_t source_index;
    uint8_t language, reserved;
    unsigned char source_sha256[32];
    int16_t settings[8];
    dok_level_span palette, light;
    uint32_t ignored_step_ver, truncated_messages;
    uint16_t object_count, glyph_index[256];
    dok_level_object objects[DOK_LEVEL_MAX_OBJECTS];
    unsigned char maps[3][DOK_LEVEL_MAX_CELLS];
} dok_level_definition;

/* A seed is not a full gameplay object or packed constructor snapshot.
 * Unknown/unplaced coordinates have pose_known=0; zero backing is not a claim
 * about their native values. Repeated glyph placements update one seed. */
typedef struct dok_world_object_seed {
    dok_object_ref reference;
    int32_t x, y, z;
    int16_t heading, upper_y, lower_y;
    uint16_t definition_index, class_id, placements, pose_known;
} dok_world_object_seed;
typedef struct dok_initial_world {
    const dok_level_definition *definition;
    dok_world_epoch epoch;
    dok_object_ref hero;
    uint16_t object_count, actor_placements, placed_actors;
    int16_t category99_total;
    uint16_t glyph_index[256];
    dok_world_object_seed objects[DOK_LEVEL_MAX_OBJECTS];
    unsigned char maps[4][DOK_LEVEL_MAX_CELLS];
} dok_initial_world;

/* Typed transient storage; union members have disjoint lifetimes. No casts of
 * declared byte arrays to structs, allocations, SDK or stdio in this module. */
typedef union dok_world_workspace {
    dok_level_definition level;
    dok_initial_world world;
} dok_world_workspace;
_Static_assert(sizeof(dok_world_workspace)<=DOK_NDS_SCRATCH_BYTES,
               "N05 typed workspace fits scratch ceiling");
_Static_assert(2u*sizeof(dok_level_definition)+sizeof(dok_initial_world)
               <=DOK_NDS_WORLD_BYTES,"N05 definitions and seed fit world ceiling");

/* All storage must be live, correctly typed and nonoverlapping. Candidate may
 * change on failure, out remains unchanged. Do not pass a live world as a
 * candidate. parse accepts RU/EN for oracle/presentation; build accepts RU only.
 * These are bounded original-sample profiles, not arbitrary mod support. */
dok_world_status dok_level_parse(const dok_asset_view *view,
    dok_level_definition *candidate, dok_level_definition *out);
dok_world_status dok_level_validate_maps(const dok_level_definition *level,
    const dok_asset_view *maps);
dok_world_status dok_initial_world_build(const dok_level_definition *effective,
    dok_world_epoch epoch, dok_initial_world *candidate, dok_initial_world *out);
/* NULL if absent. Returned pointers borrow level. */
const dok_level_field *dok_level_field_at(const dok_level_object *object,
    uint16_t native_offset);
uint32_t dok_level_span_equal(const dok_level_definition *level,
    dok_level_span span, const unsigned char *bytes, uint32_t count);
#endif
