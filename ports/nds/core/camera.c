#include "dok/camera.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#define FULL_TURN UINT32_C(23592960)
#define HALF_TURN INT64_C(11796480)

void dok_camera_reset(dok_camera_history *history)
{
    if (history != NULL) memset(history, 0, sizeof(*history));
}

dok_status dok_camera_commit(dok_camera_history *history,
                             const dok_motion_state *state, uint32_t tick)
{
    dok_camera_snapshot snapshot;
    uint32_t interval;
    if (history == NULL || state == NULL) return DOK_STATUS_INVALID_ARGUMENT;
    if (history->count > 2u || state->heading < 0 || state->heading > 360)
        return DOK_STATUS_INVALID_STATE;
    snapshot.pose.x=state->x; snapshot.pose.y=state->y; snapshot.pose.z=state->z;
    snapshot.pose.heading_q16=((uint32_t)state->heading%360u)*65536u;
    snapshot.tick=tick;
    interval=history->count == 0u ? 0u : tick-history->current.tick;
    if (interval >= UINT32_C(0x80000000)) return DOK_STATUS_INVALID_STATE;
    if (interval == 0u) {
        history->previous=snapshot; history->current=snapshot; history->count=1u;
    } else {
        history->previous=history->current; history->current=snapshot;
        history->count=2u;
    }
    return DOK_STATUS_OK;
}

static int valid(const dok_camera_history *history)
{
    return history->count >= 1u && history->count <= 2u &&
           history->previous.pose.heading_q16 < FULL_TURN &&
           history->current.pose.heading_q16 < FULL_TURN &&
           (history->count == 1u ||
            (history->current.tick-history->previous.tick != 0u &&
             history->current.tick-history->previous.tick < UINT32_C(0x80000000)));
}

static int coordinate(int32_t from, int32_t to, int32_t origin,
                       uint32_t numerator, uint32_t denominator, int32_t *out)
{
    int64_t value=(int64_t)origin+
        ((int64_t)to-from)*(int64_t)numerator/(int64_t)denominator;
    if (value < INT32_MIN || value > INT32_MAX) return 0;
    *out=(int32_t)value;
    return 1;
}

static dok_status blend(const dok_camera_history *history,
                         const dok_camera_pose *origin, uint32_t numerator,
                         uint32_t denominator, dok_camera_pose *out)
{
    const dok_camera_pose *a=&history->previous.pose, *b=&history->current.pose;
    dok_camera_pose result;
    int64_t angle=(int64_t)b->heading_q16-a->heading_q16, value;
    if (!coordinate(a->x,b->x,origin->x,numerator,denominator,&result.x) ||
        !coordinate(a->y,b->y,origin->y,numerator,denominator,&result.y) ||
        !coordinate(a->z,b->z,origin->z,numerator,denominator,&result.z))
        return DOK_STATUS_LIMIT_REACHED;
    if (angle > HALF_TURN) angle-=FULL_TURN;
    if (angle < -HALF_TURN) angle+=FULL_TURN;
    value=(int64_t)origin->heading_q16+angle*numerator/denominator;
    value%=(int64_t)FULL_TURN;
    if (value < 0) value+=FULL_TURN;
    result.heading_q16=(uint32_t)value;
    *out=result;
    return DOK_STATUS_OK;
}

dok_status dok_camera_predict(const dok_camera_history *history, uint32_t now,
                              uint32_t horizon, dok_camera_pose *out)
{
    uint32_t elapsed;
    if (history == NULL || out == NULL || horizon > DOK_MOTION_HERO_INTERVAL)
        return DOK_STATUS_INVALID_ARGUMENT;
    if (!valid(history)) return DOK_STATUS_INVALID_STATE;
    elapsed=now-history->current.tick;
    if (elapsed >= UINT32_C(0x80000000)) return DOK_STATUS_INVALID_STATE;
    if (history->count == 1u || horizon == 0u) {
        *out=history->current.pose; return DOK_STATUS_OK;
    }
    if (elapsed > horizon) elapsed=horizon;
    return blend(history,&history->current.pose,elapsed,
                 history->current.tick-history->previous.tick,out);
}

dok_status dok_camera_interpolate(const dok_camera_history *history,
                                  uint32_t alpha_q16, dok_camera_pose *out)
{
    if (history == NULL || out == NULL || alpha_q16 > 65536u)
        return DOK_STATUS_INVALID_ARGUMENT;
    if (!valid(history)) return DOK_STATUS_INVALID_STATE;
    if (history->count == 1u) { *out=history->current.pose; return DOK_STATUS_OK; }
    return blend(history,&history->previous.pose,alpha_q16,65536u,out);
}

uint32_t dok_camera_heading(const dok_camera_pose *pose)
{
    return pose == NULL ? 0u : ((pose->heading_q16+32768u)/65536u)%360u;
}
