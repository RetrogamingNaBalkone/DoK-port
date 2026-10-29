#ifndef DOK_NDS_INPUT_H
#define DOK_NDS_INPUT_H

#include "dok/contracts.h"

enum {
    DOK_INPUT_SAMPLE_LIMIT = 64,
    DOK_INPUT_NATIVE_TURN_POS = 0x00000004u,
    DOK_INPUT_NATIVE_TURN_NEG = 0x00000008u,
    DOK_INPUT_NATIVE_FORWARD = 0x00000010u,
    DOK_INPUT_NATIVE_BACKWARD = 0x00000020u,
    DOK_INPUT_NATIVE_FIRE = 0x00000200u,
    DOK_INPUT_NATIVE_USE = 0x00000800u,
    DOK_INPUT_NATIVE_RUN = 0x00002000u,
    DOK_INPUT_NATIVE_STRAFE_LEFT = 0x00080000u,
    DOK_INPUT_NATIVE_STRAFE_RIGHT = 0x00100000u
};

/* A platform sample with its monotonic N03 counter timestamp. Counter counts
 * are not N08's wrapping uint32 native ticks. Equal timestamps are ordered by
 * the strictly increasing source sequence. Never serialize native structs. */
typedef struct dok_input_sample {
    dok_input_frame frame;
    uint64_t counter;
} dok_input_sample;

/* Physical transitions are mapped independently. Opposite movement/turn bits
 * remain set together. X+B in one sample produces one shared native Use bit;
 * separate samples remain separate actions. Touch has no native command. */
typedef struct dok_input_commands {
    uint32_t native_held;
    uint32_t native_pressed;
    uint32_t native_released;
    uint32_t next_weapon_pressed;
    uint32_t pause_pressed;
} dok_input_commands;

/* Caller-owned, single-thread storage. No malloc or IRQ producer. Exposed
 * private fields provide correctly typed C11 storage; use the API to mutate.
 * A full queue fails transactionally, never overwrites/coalesces transitions.
 * Caller must retain/retry a rejected sample or enter an explicit fault state
 * before polling again. Capture alone does not execute gameplay commands. */
typedef struct dok_input_buffer {
    dok_input_sample _samples[DOK_INPUT_SAMPLE_LIMIT];
    uint64_t _last_sequence;
    uint64_t _last_counter;
    uint64_t _last_cutoff;
    uint32_t _tag;
    uint32_t _head;
    uint32_t _count;
    uint32_t _raw_held;
    uint32_t _raw_touch;
    uint32_t _blocked;
    uint32_t _touch_blocked;
    uint32_t _has_capture;
    uint32_t _has_cutoff;
} dok_input_buffer;

/* Initialize unused storage; does not inspect its previous contents. */
dok_status dok_input_buffer_initialize(dok_input_buffer *buffer);
/* Source sequence must be nonzero and increase; counter may stay equal but
 * must never go backward, including behind an already consumed cutoff.
 * N03 shape is validated, exact edge history is the sampler's responsibility:
 * a new buffer can attach to an already running source. Output/state preserve
 * on refusal. Each successful capture retains one sample, even unchanged. */
dok_status dok_input_capture(dok_input_buffer *buffer,
                             const dok_input_frame *frame, uint64_t counter);
/* Take at most one ordered sample with timestamp <= cutoff. Cutoffs must be
 * nondecreasing. Empty/not-yet-due is OK, available=0 and out is unchanged.
 * out and available are distinct correctly typed objects outside buffer. */
dok_status dok_input_pop_through(dok_input_buffer *buffer, uint64_t cutoff,
                                 dok_input_sample *out, uint32_t *available);
/* Synchronous UI transition after the latest physical capture: discard queued
 * gameplay samples and swallow currently held keys/touch through their release.
 * The swallowed release produces no stray released edge. Polling continues in
 * UI; call this after each UI poll and on return to gameplay. */
dok_status dok_input_ui_boundary(dok_input_buffer *buffer);
/* Pure mapping of one valid raw/filtered N03 frame. Does not mutate source,
 * buffer or world; output unchanged on invalid input. Short press preservation
 * needs ordered consumption plus the gameplay owner's explicit action latch. */
dok_status dok_input_map(const dok_input_frame *frame,
                         dok_input_commands *out);

#endif
