#ifndef DOK_NDS_GAMEFLOW_H
#define DOK_NDS_GAMEFLOW_H
#include "dok/motion.h"

enum { DOK_FLOW_ALIVE=0, DOK_FLOW_RETRY=1, DOK_FLOW_GAMEOVER=2, DOK_FLOW_FINISH=3 };
/* Native signed-WORD DEC, including zero/-32768 wrap. FINISH has no life cost. */
uint32_t dok_gameflow_check(int16_t health,int16_t *lives);
/* Only the modeled members of native actor+26..43. Inertia, flags, input
 * history and every member outside that original byte range remain intact. */
void dok_gameflow_restore_motion(dok_motion_state *,const dok_motion_state *spawn);
#endif
