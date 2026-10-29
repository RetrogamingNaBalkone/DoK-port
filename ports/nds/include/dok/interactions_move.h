#ifndef DOK_NDS_INTERACTIONS_MOVE_H
#define DOK_NDS_INTERACTIONS_MOVE_H
#include "dok/interactions.h"
/* Context is dok_collision_state*. N11 delegates to the distinct public direct
 * wrapper, preserving legitimate C2=4 displacement350 and inertial scalars. */
dok_status dok_interactions_move(void *context, dok_object_ref actor,
    dok_motion_state *motion, int32_t x, int32_t z);
#endif
