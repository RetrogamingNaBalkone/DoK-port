#ifndef DOK_MOVEMENT_H
#define DOK_MOVEMENT_H
#include "dok/runtime.h"

enum {
    DOK_INPUT_LEFT=4, DOK_INPUT_RIGHT=8, DOK_INPUT_FORWARD=0x10,
    DOK_INPUT_BACK=0x20, DOK_INPUT_FIRE=0x200, DOK_INPUT_USE=0x800,
    DOK_INPUT_RUN=0x2000, DOK_INPUT_STRAFE_LEFT=0x80000,
    DOK_INPUT_STRAFE_RIGHT=0x100000, DOK_INPUT_SLOW=0x4000000
};
/* Native +9A does not yet have a runtime actor field. Keep it per actor,
 * never globally shared with AI. Zero at reset; read only after a move command. */
typedef struct { uint32_t remembered_flags; } dok_motion;
typedef struct { uint32_t held,pressed,released,previous; } dok_input;
void dok_input_sample(dok_input *input,uint16_t gba_keys_down);
/* Active-high GBA key bits; raw opposing commands are emitted together.
 * D-pad moves/turns; L/R strafe; B runs; A fire, Start use (edge). Select UI. */
uint32_t dok_input_commands(const dok_input *input);
int16_t dok_motion_heading(int32_t heading);
/* Optional G08 seam: signed quarter-distance, strafe flag, three ordered calls
 * per active translation command. NULL means free flight, without map writes.
 * Caller supplies original-clock elapsed ticks; this is NOT frame count.
 * Returns whether x/z changed. y and vertical bounds are untouched. */
typedef void (*dok_motion_move)(dok_runtime_actor *,int16_t,int,void *);
/* G07 deliberate gameplay correction: instant translation start/stop/reversal.
 * Opposing translation commands cancel on each axis. */
int dok_motion_update(dok_runtime_actor *actor,dok_motion *motion,uint32_t commands,
    uint32_t elapsed,dok_motion_move move,void *context);
/* Original acceleration remains available only as evidence/reference. */
int dok_motion_original(dok_runtime_actor *actor,dok_motion *motion,uint32_t commands,
    uint32_t elapsed,dok_motion_move move,void *context);
/* Same wrapped low32 IMUL/SAR15 displacement used before native collision. */
void dok_motion_free(dok_runtime_actor *actor,int16_t distance,int strafe,void *context);
#endif
