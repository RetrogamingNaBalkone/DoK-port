#ifndef DOK_NDS_CAMERA_H
#define DOK_NDS_CAMERA_H

#include "dok/motion.h"

/* Presentation is a separate value. It is never written back into motion. */
typedef struct dok_camera_pose {
    int32_t x, y, z;
    uint32_t heading_q16; /* [0,360*65536). */
} dok_camera_pose;
typedef struct dok_camera_snapshot {
    dok_camera_pose pose;
    uint32_t tick;
} dok_camera_snapshot;
typedef struct dok_camera_history {
    dok_camera_snapshot previous, current;
    uint32_t count;
} dok_camera_history;

/* Reset on world epoch change, teleport, respawn or other discontinuity.
 * Pause needs no reset when the injected N08 world clock is frozen. */
void dok_camera_reset(dok_camera_history *history);
/* Publish one completed simulation observation. A zero interval replaces both
 * snapshots, clearing prediction velocity. Intervals >=2^31 are rejected.
 * Heading must be a valid simulation angle 0..360; the table endpoint360 is
 * represented as presentation0 without changing simulation storage. */
dok_status dok_camera_commit(dok_camera_history *history,
                             const dok_motion_state *state, uint32_t tick);
/* Pure bounded extrapolation of actual last committed displacement/turn.
 * horizon<=416 native ticks. First observation has no velocity. now must be
 * within the unsigned forward half-range of current.tick. Caller injects time;
 * no clock, events, motion, collisions or callbacks are invoked. */
dok_status dok_camera_predict(const dok_camera_history *history, uint32_t now,
                              uint32_t horizon, dok_camera_pose *out);
/* Pure interpolation between the two committed poses, alpha 0..65536.
 * Shortest heading arc, including 359/0. Available for N10's conservative
 * presentation policy; it does not advance or modify the simulation. */
dok_status dok_camera_interpolate(const dok_camera_history *history,
                                  uint32_t alpha_q16, dok_camera_pose *out);
/* Accepted N07 renderer currently consumes integer-degree headings. Nearest
 * integer modulo360 is an explicit <=0.5degree presentation quantization. */
uint32_t dok_camera_heading(const dok_camera_pose *pose);

#endif
