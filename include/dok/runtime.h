#ifndef DOK_RUNTIME_H
#define DOK_RUNTIME_H
#include "dok/world.h"

/* Compact engine model, not a native memory overlay or disk format.
 * Native offsets in ROM field records are evidence tags, never addresses.
 * G03 must encode integers explicitly; only the view below contains pointers. */
#define DOK_RUNTIME_MAX_OBJECTS 256
#define DOK_RUNTIME_MAX_ACTORS 128 /* original maximum106;22 spare actor slots */
#define DOK_RUNTIME_MAX_FIELDS 2048 /* original maximum1655 */
#define DOK_RUNTIME_MAX_TEXT 16384
#define DOK_RUNTIME_MAX_CELLS 4096
#define DOK_RUNTIME_NONE UINT16_MAX

typedef struct { uint16_t offset, size; } dok_runtime_span;
typedef struct {
    uint16_t native_offset;
    uint8_t kind, reserved; /* dok_level_kind */
    union { int32_t number; dok_runtime_span bytes; } value;
} dok_runtime_field;
typedef struct {
    uint16_t class_id, flags, sprite_flags;
    int16_t width_parameter, height_parameter;
    uint8_t glyph, marker;
    uint16_t actor_index; /* zero-based; NONE for non-actors */
    uint16_t field_begin, field_count, placements;
    dok_runtime_span sprite, animation, sound;
} dok_runtime_definition;

enum {
    DOK_RUNTIME_ANIMATION_A=1u, DOK_RUNTIME_ANIMATION_B=2u,
    DOK_RUNTIME_INTERVAL=4u
};
enum {
    DOK_RUNTIME_X=1u, DOK_RUNTIME_Y=2u, DOK_RUNTIME_Z=4u,
    DOK_RUNTIME_BOUNDS=8u
};
typedef struct {
    int32_t parameter_66;
    int16_t parameter_5c, parameter_5e, state_5a, state_58, state_56;
    uint16_t original_flags;
    uint8_t parameter_54, parameter_55;
} dok_runtime_door;
typedef struct { int16_t state, command, progress, target; } dok_runtime_switch;
typedef struct {
    int32_t velocity, y;
    int16_t damage, half_damage, layer, state;
    uint8_t previous_glyph, reserved[3];
} dok_runtime_projectile;
typedef struct {
    int16_t health, state, course_counter;
    uint8_t auxiliary_e6, course_counter_known;
} dok_runtime_monster;
typedef struct {
    uint16_t flags, frame, animation_budget, interval;
    int16_t animation_a, animation_b;
    uint16_t known, reserved;
    union {
        dok_runtime_door door;
        dok_runtime_switch toggle;
        dok_runtime_projectile projectile;
        dok_runtime_monster monster;
        int16_t weapon_layer;
    } state;
} dok_runtime_object;
typedef struct {
    uint32_t known;
    int32_t x, y, z, base_speed, step_distance, accumulator_a, accumulator_b;
    uint32_t motion_flags;
    int16_t heading, angle_34, mode, layer;
    int16_t direction, step, ready, command, command_84, parameter_98;
    int16_t bound_9e, bound_a0;
    uint8_t glyph, previous_glyph;
    uint8_t reserved[2];
} dok_runtime_actor;
typedef struct {
    int16_t health, energy, health_max, energy_max;
    int16_t parameters_66_70[6];
    uint16_t keys;
    int16_t parameters_76_7a[3];
    int16_t selected_weapon, pending_weapon, lives, score;
    uint16_t weapons[9]; /* object index+1;0=null, never far-pointer tokens */
    uint16_t reserved;
    uint32_t auxiliary_da;
} dok_runtime_player;
/* Borrowed ROM view. Every referenced array must outlive every runtime using it.
 * Maps are MMap/TMap/FCMap in verified runtime orientation. Strings are raw
 * CP866, no terminator required. Text/resource records contain offsets only. */
typedef struct {
    uint16_t settings[DOK_LEVEL_SETTING_COUNT];
    uint16_t object_count, actor_count, field_count, text_size, cells;
    uint16_t player_index; /* object index+1;0=no placed player */
    int16_t pickup_99_total;
    uint16_t glyph_index[256]; /* object index+1;0=undefined */
    dok_runtime_span palette, light;
    const dok_runtime_definition *definitions;
    const dok_runtime_field *fields;
    const uint8_t *text, *maps;
    const dok_runtime_object *initial_objects;
    const dok_runtime_actor *initial_actors;
    const dok_runtime_player *initial_player;
} dok_runtime_level;
/* Host converter backing storage; never allocate this in GBA RAM. G03 emits
 * only used records to ROM and constructs a const view without LEVEL parsing. */
typedef struct {
    dok_runtime_definition definitions[DOK_RUNTIME_MAX_OBJECTS];
    dok_runtime_field fields[DOK_RUNTIME_MAX_FIELDS];
    uint8_t text[DOK_RUNTIME_MAX_TEXT], maps[3*DOK_RUNTIME_MAX_CELLS];
    dok_runtime_object objects[DOK_RUNTIME_MAX_OBJECTS];
    dok_runtime_actor actors[DOK_RUNTIME_MAX_ACTORS];
    dok_runtime_player player;
} dok_runtime_storage;
/* Caller owns RAM. reset clears all slots including reserve/visited, restores
 * initial values and borrows level; release only clears RAM and drops its view.
 * Actor reserve exists for future dynamic allocation; allocation is not G02. */
typedef struct {
    const dok_runtime_level *level;
    dok_runtime_object objects[DOK_RUNTIME_MAX_OBJECTS];
    dok_runtime_actor actors[DOK_RUNTIME_MAX_ACTORS];
    dok_runtime_player player;
    uint8_t maps[4*DOK_RUNTIME_MAX_CELLS];
    uint16_t object_count, actor_count, player_index;
    int16_t pickup_99_total;
} dok_runtime;
typedef enum {
    DOK_RUNTIME_OK, DOK_RUNTIME_ARGUMENT, DOK_RUNTIME_CAPACITY,
    DOK_RUNTIME_DEFINITION, DOK_RUNTIME_SOURCE
} dok_runtime_status;
typedef struct {
    dok_runtime_status status;
    uint16_t object_index, native_offset;
} dok_runtime_error;
/* Host-only conversion from a successfully built stage6 world. Distinct input
 * and output storage required. On failure discard out and backing storage. */
dok_runtime_status dok_runtime_build(dok_runtime_level *out,
    dok_runtime_storage *storage,const dok_world *source,dok_runtime_error *error);
/* Validate a borrowed ROM view without allocating a mutable world. */
dok_runtime_status dok_runtime_validate_level(const dok_runtime_level *level);
/* Validation failure leaves runtime untouched. No allocation/host calls. */
dok_runtime_status dok_runtime_reset(dok_runtime *runtime,const dok_runtime_level *level);
void dok_runtime_release(dok_runtime *runtime);
const dok_runtime_definition *dok_runtime_definition_at(const dok_runtime_level *level,uint16_t object_index);
const dok_runtime_actor *dok_runtime_actor_at(const dok_runtime *runtime,uint16_t object_index);
int dok_runtime_position(const dok_runtime *runtime,uint16_t object_index,int32_t *x,int32_t *y,int32_t *z);
/* Diagnostic scalar adapter: known1/2/4-byte ranges only; output untouched on
 * unknown/failure. Reconstructs evidence bytes on demand, no native snapshot.
 * Native pointer fields stay unknown; null weapons are represented as IDs. */
int dok_runtime_read_u(const dok_runtime *runtime,uint16_t object_index,size_t offset,unsigned width,uint32_t *out);
int dok_runtime_read_s(const dok_runtime *runtime,uint16_t object_index,size_t offset,unsigned width,int32_t *out);
#endif


