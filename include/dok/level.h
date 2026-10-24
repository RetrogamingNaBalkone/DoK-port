#ifndef DOK_LEVEL_H
#define DOK_LEVEL_H
#include <stddef.h>
#include <stdint.h>

/* Borrowed CP866 bytes. Offsets are relative to level.source, never native pointers. */
typedef struct { uint32_t offset, size; } dok_level_span;
typedef enum {
    DOK_LEVEL_I16, DOK_LEVEL_I32, DOK_LEVEL_CHAR,
    DOK_LEVEL_IMAGE, DOK_LEVEL_ANIMATION, DOK_LEVEL_SOUND, DOK_LEVEL_TEXT
} dok_level_kind;
typedef struct {
    uint16_t native_offset;
    uint8_t kind;
    union { int32_t number; dok_level_span bytes; } value;
} dok_level_field;
#define DOK_LEVEL_MAX_FIELDS 19
typedef struct {
    uint8_t glyph, marker;
    uint16_t class_id, flags, sprite_flags;
    int16_t width_parameter, height_parameter;
    dok_level_span sprite, animation, sound, raw_tail;
    uint32_t line;
    uint8_t field_count;
    dok_level_field fields[DOK_LEVEL_MAX_FIELDS];
} dok_level_object;
typedef enum {
    DOK_LEVEL_WIDTH, DOK_LEVEL_HEIGHT, DOK_LEVEL_LENGTH, DOK_LEVEL_CELL_SIDE,
    DOK_LEVEL_CELL_HEIGHT, DOK_LEVEL_FLOOR_COLOR, DOK_LEVEL_CEILING_COLOR,
    DOK_LEVEL_MIN_DISTANCE, DOK_LEVEL_SETTING_COUNT
} dok_level_setting;
typedef struct {
    const uint8_t *source;
    size_t source_size;
    uint16_t settings[DOK_LEVEL_SETTING_COUNT];
    dok_level_span palette, light;
    dok_level_object *objects;
    size_t object_count;
    /* 0=TMap, 1=MMap, 2=FCMap; packed width*length bytes each. */
    uint8_t *maps;
    uint16_t glyph_index[256]; /* object index + 1; zero means undefined */
    uint32_t ignored_step_ver, truncated_messages;
} dok_level;
typedef enum {
    DOK_LEVEL_OK, DOK_LEVEL_ARGUMENT, DOK_LEVEL_SYNTAX, DOK_LEVEL_RANGE,
    DOK_LEVEL_CAPACITY, DOK_LEVEL_DUPLICATE, DOK_LEVEL_INCOMPLETE
} dok_level_status;
typedef struct { dok_level_status status; uint32_t line; } dok_level_error;

/* Validated original-data profile: dimensions 1..64, height=1, all sections.
 * No allocation; source, objects and maps must remain alive and must not overlap.
 * On failure output/storage may be partially written; discard them.
 * Messages preserve leading spaces and the native first 39 bytes.
 * Values describe parsed definitions, before constructors/runtime transforms.
 */
dok_level_status dok_level_parse(dok_level *out, const void *source, size_t size,
    dok_level_object *objects, size_t capacity, uint8_t *maps, size_t map_capacity,
    dok_level_error *error);
#endif
