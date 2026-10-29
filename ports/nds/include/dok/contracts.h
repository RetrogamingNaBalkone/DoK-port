#ifndef DOK_NDS_CONTRACTS_H
#define DOK_NDS_CONTRACTS_H

#include <stdint.h>

/* Native C11 interfaces, never a disk, DOS-memory or IPC layout. */
typedef enum dok_status {
    DOK_STATUS_OK = 0,
    DOK_STATUS_INVALID_ARGUMENT = 1,
    DOK_STATUS_NO_SPACE = 2,
    DOK_STATUS_INVALID_STATE = 3,
    DOK_STATUS_STALE_REFERENCE = 4,
    DOK_STATUS_UNSUPPORTED = 5,
    DOK_STATUS_LIMIT_REACHED = 6
} dok_status;

typedef uint32_t dok_asset_id;
typedef uint32_t dok_object_id;
typedef uint32_t dok_world_epoch;

/* All three components must be nonzero. IDs are not glyphs or addresses.
 * A world owns IDs; reuse of an object slot requires a new generation.
 * Generation/epoch exhaustion must fail rather than wrap to a live identity. */
typedef struct dok_object_ref {
    dok_world_epoch world_epoch;
    dok_object_id object_id;
    uint32_t generation;
} dok_object_ref;

enum {
    DOK_BUTTON_A = 1u << 0,
    DOK_BUTTON_B = 1u << 1,
    DOK_BUTTON_SELECT = 1u << 2,
    DOK_BUTTON_START = 1u << 3,
    DOK_BUTTON_RIGHT = 1u << 4,
    DOK_BUTTON_LEFT = 1u << 5,
    DOK_BUTTON_UP = 1u << 6,
    DOK_BUTTON_DOWN = 1u << 7,
    DOK_BUTTON_R = 1u << 8,
    DOK_BUTTON_L = 1u << 9,
    DOK_BUTTON_X = 1u << 10,
    DOK_BUTTON_Y = 1u << 11,
    DOK_BUTTON_MASK = 0x0FFFu,
    DOK_TOUCH_HELD = 1u,
    DOK_TOUCH_PRESSED = 2u,
    DOK_TOUCH_RELEASED = 4u
};

/* One platform sample; sequence does not encode time or a scheduler tick.
 * Edge bits describe transitions between samples, not a queue of all events.
 * Touch is exclusively the lower 256x192 screen. Coordinates are meaningful
 * while held; released coordinates are ignored. N09 owns gameplay mapping. */
typedef struct dok_input_frame {
    uint64_t sequence;
    uint32_t held;
    uint32_t pressed;
    uint32_t released;
    uint32_t touch_flags;
    int16_t touch_x;
    int16_t touch_y;
} dok_input_frame;

typedef uint64_t (*dok_read_counter_fn)(void *context);
typedef void (*dok_diagnostic_fn)(void *context, uint32_t severity,
                                  const char *message, uint32_t byte_count);

/* Counter frequency = numerator/denominator counts per second, both nonzero.
 * read_counter is monotonic for the service lifetime. N08 owns conversion to
 * the original uint32 scheduler clock, pause, dispatch and overflow semantics.
 * Context remains caller-owned. Diagnostic may be NULL; messages are borrowed
 * only during the callback. Core lifecycle invokes neither callback. */
typedef struct dok_platform_services {
    void *context;
    dok_read_counter_fn read_counter;
    uint32_t counter_hz_numerator;
    uint32_t counter_hz_denominator;
    dok_diagnostic_fn diagnostic;
} dok_platform_services;

/* Immutable descriptors copied by value into the shell. The optional opaque
 * implementation is caller-owned and must outlive the shell. NULL denotes
 * metadata only, not a loaded/verified package or an implemented profile.
 * N04/N05 define typed payloads; no decoder or gameplay table is implied here. */
typedef struct dok_asset_catalog {
    uint32_t identity;
    uint32_t revision;
    const void *implementation;
} dok_asset_catalog;

typedef struct dok_balance_profile {
    uint32_t identity;
    uint32_t revision;
    const void *implementation;
} dok_balance_profile;

/* Immutable presentation lease, not a copy of the world or GPU command ABI.
 * Owner retains payload until explicit release or agreed next prepare. No
 * gameplay mutation/reset may invalidate an outstanding lease silently.
 * N07/N08/N20 define acquisition, release and core-applied visibility/effects;
 * N03 supplies the descriptor only and never manufactures a scene. */
typedef struct dok_scene_snapshot {
    dok_world_epoch world_epoch;
    uint32_t lease_id;
    uint64_t sequence;
    const void *presentation;
} dok_scene_snapshot;

typedef struct dok_game_config {
    dok_platform_services platform;
    dok_asset_catalog assets;
    dok_balance_profile balance;
} dok_game_config;

enum {
    DOK_GAME_INITIALIZED = 1u,
    DOK_GAME_SHUT_DOWN = 2u
};

/* Caller owns a correctly typed static/automatic object. Private fields are
 * exposed to provide native C11 storage without malloc or byte-buffer casts;
 * callers must use API functions to mutate them. This is an initialized shell,
 * not a loaded world. Future world storage is separately specified in N05. */
typedef struct dok_game_state {
    dok_game_config _config;
    uint32_t _lifecycle_generation;
    uint32_t _state;
} dok_game_state;

typedef struct dok_game_info {
    uint32_t state;
    uint32_t lifecycle_generation;
    uint32_t asset_identity;
    uint32_t asset_revision;
    uint32_t balance_identity;
    uint32_t balance_revision;
    dok_world_epoch world_epoch;
    uint32_t has_world;
} dok_game_info;

uint32_t dok_object_ref_is_valid(dok_object_ref reference);
uint32_t dok_object_ref_equal(dok_object_ref left, dok_object_ref right);
dok_status dok_input_frame_validate(const dok_input_frame *input);
dok_status dok_game_config_validate(const dok_game_config *config);

/* Initialize only unused or shut-down storage. Does not read old state; may
 * receive a config borrowed from this object's former configuration. All
 * validation failures preserve state. No resources are allocated or loaded. */
dok_status dok_game_initialize(dok_game_state *game,
                               const dok_game_config *config);
/* Reset preserves borrowed descriptors; generation never wraps. N03 has no
 * world or outstanding scene lease. Future implementations must reject reset
 * while a lease is live or release it at an explicitly ordered core phase. */
dok_status dok_game_reset(dok_game_state *game);
/* Shutdown releases borrowed references, not caller memory. Idempotent after
 * successful initialization. An arbitrary uninitialized object is not valid. */
dok_status dok_game_shutdown(dok_game_state *game);
/* Output is unchanged on failure and must be a separate correctly typed object. */
dok_status dok_game_inspect(const dok_game_state *game, dok_game_info *out);

#endif
