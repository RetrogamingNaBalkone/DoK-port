#ifndef DOK_NDS_INTERACTIONS_H
#define DOK_NDS_INTERACTIONS_H
#include "dok/collision.h"
#include "dok/events.h"

enum { DOK_INTERACTION_OPEN=0x1101, DOK_INTERACTION_CLOSE=0x1102,
       DOK_INTERACTION_AUTO=0x1103 };
enum { DOK_INTERACTION_LOCKED=1, DOK_INTERACTION_SOUND_ENSURE=2,
       DOK_INTERACTION_SOUND_STOP=3, DOK_INTERACTION_STATUS=4,
       DOK_INTERACTION_STAT=5, DOK_INTERACTION_FINISH=6 };
typedef struct dok_interaction_output {
    dok_object_ref object, source;
    uint32_t kind;
    int16_t category, value;
    uint16_t key_mask, reserved;
} dok_interaction_output;
/* Synchronous ordered output, borrowed only during this call. The sink must
 * not reenter interactions/events, mutate the world, or retain this pointer. */
typedef void (*dok_interaction_sink_fn)(void *context,
    const dok_interaction_output *output);
/* One direct displacement, not N09's three quarter steps. Preserve all motion
 * fields except the accepted x/z. N10 admission/occupancy remain authoritative. */
typedef dok_status (*dok_interaction_move_fn)(void *context,
    dok_object_ref actor, dok_motion_state *motion, int32_t x, int32_t z);
typedef struct dok_interaction_services {
    void *sink_context;
    dok_interaction_sink_fn sink;
    void *move_context;
    dok_interaction_move_fn move;
    void *trig_context;
    dok_motion_trig_fn trig;
} dok_interaction_services;
typedef struct dok_interaction_door {
    int16_t phase, extent;
    uint8_t state, ready, marker, reserved;
} dok_interaction_door;
typedef struct dok_interaction_phase_view {
    dok_world_epoch world_epoch;
    uint32_t revision;
    int16_t phase[256], extent[256];
} dok_interaction_phase_view;
/* Caller-owned native C storage. Maps, flags and occupancy are NOT copied.
 * definitions/collision/scheduler/services outlive this exclusive borrower. */
typedef struct dok_interactions_state {
    dok_initial_world *_world;
    dok_collision_state *_collision;
    dok_event_scheduler *_scheduler;
    dok_interaction_services _services;
    dok_interaction_door doors[256]; /* indexed glyph, not object ID */
    dok_object_ref previous_floor;
    uint32_t _tag, _epoch, revision, schedule_refusals, fault;
    uint32_t _busy, _registered;
} dok_interactions_state;

/* Exact original C2 update. frame_ticks==0 is rejected instead of DIV fault.
 * C2 is an explicit service sample: floor uses signed DWORD, door signed WORD. */
dok_status dok_interactions_cadence(uint32_t old_c2, uint32_t frame_ticks,
    uint32_t *out_c2);
dok_status dok_interactions_bind(dok_interactions_state *state,
    dok_initial_world *world, dok_collision_state *collision,
    dok_event_scheduler *scheduler, const dok_interaction_services *services);
/* Once, before dispatch: all class7 owners, native capacity4 (3 usable slots).
 * A failure can leave earlier registrations; caller resets the complete world. */
dok_status dok_interactions_register(dok_interactions_state *state);
/* source may be the all-zero reference for base-handler diagnostic vectors.
 * Door Use needs a valid source; class13 bypasses keys. Keys are borrowed and
 * never consumed. Schedule NO_SPACE is native refusal, counted, returns OK. */
dok_status dok_interactions_event(dok_interactions_state *state,
    dok_object_ref target, dok_object_ref source, uint32_t *bits,
    uint16_t source_keys, uint32_t c2);
/* One point one cell ahead in TMap, not raycast. Hero consumes Use even when
 * the target's permitted no-op leaves its local event unchanged. */
dok_status dok_interactions_use(dok_interactions_state *state,
    dok_object_ref hero, const dok_motion_state *motion, uint32_t *bits,
    uint16_t keys, uint32_t c2);
/* Pre-motion FCMap only (pickups belong to N14). Repeated sensor glyph cells
 * share one identity. score is borrowed signed WORD; FINISH is only output. */
dok_status dok_interactions_floor(dok_interactions_state *state,
    dok_object_ref hero, dok_motion_state *motion, int16_t score, uint32_t c2);
/* Called by the shell's N08 router, with no other collision/render borrower. */
dok_status dok_interactions_callback(dok_interactions_state *state,
    dok_object_ref owner, uint16_t callback);
dok_status dok_interactions_phases(const dok_interactions_state *state,
    dok_interaction_phase_view *out);
#endif
