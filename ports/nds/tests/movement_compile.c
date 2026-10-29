#include "dok/input.h"
#include "dok/controls.h"
#include "dok/motion.h"
#include "dok/camera.h"
#include "dok/time.h"
#include "dok/events.h"
#include "input_cases.h"
#include "motion_cases.h"
#include "movement_replay.h"
#include "movement_probe.h"

_Static_assert(DOK_MOTION_HERO_INTERVAL == 416, "source hero event interval");
_Static_assert(sizeof(dok_movement_replay_workspace) <= DOK_NDS_SCRATCH_BYTES,
               "typed replay workspace must fit accepted scratch");
_Static_assert(sizeof(NdsMovementProbe) == 288u, "N09 observation ABI v1");
_Static_assert(sizeof(NdsMovementTraceRecord) == 288u, "N09 trace ABI v1");

int dok_movement_header_compile(void)
{
    return (int)(sizeof(dok_motion_state) + sizeof(dok_camera_history) +
                 sizeof(dok_control_latch) + sizeof(dok_input_buffer));
}
