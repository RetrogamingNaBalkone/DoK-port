#ifndef DOK_NDS_CONTROLS_H
#define DOK_NDS_CONTROLS_H

#include "dok/input.h"

/* Service commands are not raw N03 frames: a tap may have both edges here.
 * Every physical press/release is retained until a service consumes it.
 * A short movement/run/fire press contributes at least one service pulse.
 * Several presses on one button take several services, rather than disappearing.
 * Use is triggered on release: X+B releases in one sample are one Use;
 * separate release samples are separate Uses. Held X/B never repeat Use. */
typedef struct dok_control_commands {
    uint32_t physical_held, physical_pressed, physical_released;
    uint32_t native_commands, next_weapon_pressed, pause_pressed, use_triggered;
} dok_control_commands;

typedef struct dok_control_latch {
    uint32_t _presses[12], _releases[12];
    uint32_t _held, _uses, _tag;
    uint64_t _last_sequence;
} dok_control_latch;

dok_status dok_controls_initialize(dok_control_latch *latch);
/* Feed each filtered sample in pop order; no clocks, motion or events run.
 * Overflow/invalid order is an explicit refusal, preserving the latch. */
dok_status dok_controls_feed(dok_control_latch *latch,
                              const dok_input_frame *frame);
/* Consume at most one edge of each physical button and one Use per service.
 * Latest movement/fire/run levels remain; Use is emitted only from a consumed
 * pending Use release, never from make or held X/B. Opposite commands remain. */
dok_status dok_controls_take(dok_control_latch *latch,
                              dok_control_commands *out);
/* Alongside input_ui_boundary: drop pre-transition gameplay actions/held.
 * Input buffer, rather than this latch, suppresses physical keys until release. */
dok_status dok_controls_ui_boundary(dok_control_latch *latch);

#endif
