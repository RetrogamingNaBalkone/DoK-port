#ifndef DOK_NDS_MOTION_H
#define DOK_NDS_MOTION_H

#include "dok/contracts.h"

enum { DOK_MOTION_HERO_INTERVAL = 416, DOK_MOTION_MAX_SUBSTEPS = 12 };
#define DOK_MOTION_SLOW UINT32_C(0x04000000)

/* Native scalar values, not the packed DOS actor layout. Caller owns storage.
 * flags contain native commands and world flags; update consumes the native
 * transient mask. Heading and parameters retain signed WORD arithmetic. */
typedef struct dok_motion_state {
    uint32_t flags;
    int32_t x, y, z;
    int32_t speed, distance;
    int32_t movement_accumulator, turn_accumulator;
    uint32_t last_flags;
    int16_t heading, angle_parameter;
    int16_t turn_step, linear_step, last_turn, accel_sign;
} dok_motion_state;

typedef struct dok_motion_calculation {
    dok_motion_state state;
    uint32_t movement_flags;
    uint32_t factor;
} dok_motion_calculation;

typedef struct dok_motion_substep {
    int32_t x, y, z;
    int32_t candidate_x, candidate_z;
    int16_t distance; /* Original signed SAR16(full distance,2). */
    uint16_t strafe;
    uint32_t ordinal; /* 0..11 in forward/back/left/right order. */
} dok_motion_substep;

/* Immutable original Q15 table lookup; 0..360 are valid table entries.
 * Return nonzero on success. Services may not mutate the caller's state. */
typedef int (*dok_motion_trig_fn)(void *context, uint32_t heading,
                                  int32_t *sine, int32_t *cosine);
/* Collision rejection returns old x/z; all three substeps still execute.
 * This boundary owns future N10 occupancy/door effects. It cannot fail, call
 * update recursively, or read the caller's unpublished candidate state. */
typedef void (*dok_motion_move_fn)(void *context,
                                   const dok_motion_substep *request,
                                   int32_t *accepted_x, int32_t *accepted_z);
typedef struct dok_motion_services {
    void *trig_context;
    dok_motion_trig_fn trig;
    void *move_context;
    dok_motion_move_fn move;
} dok_motion_services;

typedef struct dok_motion_report {
    uint32_t factor, movement_flags, substeps, moved;
} dok_motion_report;

/* Explicitly initializes last_flags=0 where the DOS base constructor leaves
 * it unspecified. No clock starts here: first original callback elapsed=0. */
dok_status dok_motion_init(dok_motion_state *state, int32_t x, int32_t y,
                           int32_t z, int16_t heading, int16_t angle_parameter,
                           int32_t speed);
/* Scalar native arithmetic only. No table read, movement, clock or callback;
 * damaged WORD headings remain representable for isolated original vectors.
 * state.distance includes the final wrapper's full signed argument. */
dok_status dok_motion_calculate(const dok_motion_state *state, uint32_t elapsed,
                                dok_motion_calculation *out);
/* One native callback, never a fixed timestep or catch-up loop. All required
 * table entries are preflighted before movement callbacks. Output/state are
 * unchanged on failure; successful callbacks receive complete step requests. */
dok_status dok_motion_update(dok_motion_state *state, uint32_t elapsed,
                             const dok_motion_services *services,
                             dok_motion_report *out);
/* Explicit free-flight DIAGNOSTIC ONLY until N10 collision integration. */
void dok_motion_free_flight(void *context, const dok_motion_substep *request,
                            int32_t *accepted_x, int32_t *accepted_z);

#endif
