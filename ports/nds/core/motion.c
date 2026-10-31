#include "dok/motion.h"
#include "dok/input.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#define MOVE_MASK UINT32_C(0x00180030)
#define TURN_MASK UINT32_C(0x0000000c)

static int32_t signed32(uint32_t bits)
{
    return bits <= (uint32_t)INT32_MAX ? (int32_t)bits :
           (int32_t)((int64_t)bits - INT64_C(4294967296));
}

static int16_t word(int32_t value)
{
    uint16_t bits = (uint16_t)(uint32_t)value;
    return bits <= (uint16_t)INT16_MAX ? (int16_t)bits :
           (int16_t)((int32_t)bits - 65536);
}

static int32_t add32(int32_t a, int32_t b)
{
    return signed32((uint32_t)a + (uint32_t)b);
}

static int32_t mul32(int32_t a, int32_t b)
{
    return signed32((uint32_t)((uint64_t)(uint32_t)a * (uint32_t)b));
}

static int32_t sar(int32_t value, uint32_t shift)
{
    int64_t divisor = INT64_C(1) << shift;
    return value >= 0 ? (int32_t)((int64_t)value / divisor) :
           (int32_t)(-((-(int64_t)value + divisor - 1) / divisor));
}

dok_status dok_motion_init(dok_motion_state *state, int32_t x, int32_t y,
                           int32_t z, int16_t heading, int16_t angle_parameter,
                           int32_t speed)
{
    if (state == NULL) return DOK_STATUS_INVALID_ARGUMENT;
    memset(state, 0, sizeof(*state));
    state->x = x; state->y = y; state->z = z;
    state->heading = heading; state->angle_parameter = angle_parameter;
    state->speed = speed; state->accel_sign = 1;
    return DOK_STATUS_OK;
}

dok_status dok_motion_calculate(const dok_motion_state *state, uint32_t elapsed,
                                dok_motion_calculation *out)
{
    dok_motion_calculation result;
    dok_motion_state *s = &result.state;
    uint32_t flags;
    int32_t factor, value;
    if (state == NULL || out == NULL) return DOK_STATUS_INVALID_ARGUMENT;
    result.state = *state;
    flags = s->flags;
    s->flags &= UINT32_C(0xfbe7dfc3);
    if ((flags & MOVE_MASK) != 0u) {
        s->accel_sign = 1;
        s->last_flags = flags;
    } else {
        s->accel_sign = -1;
    }
    s->movement_accumulator = add32(s->movement_accumulator,
                                    mul32(s->accel_sign, signed32(elapsed)));
    s->turn_accumulator = add32(s->turn_accumulator, signed32(elapsed));
    if (s->movement_accumulator > 10000) s->movement_accumulator = 10000;
    if (s->movement_accumulator < 0) s->movement_accumulator = 0;
    if (s->turn_accumulator > 10000) s->turn_accumulator = 10000;
    factor = (int32_t)((elapsed << 8) / 416u);
    if ((flags & TURN_MASK) != 0u && (flags & UINT32_C(0x30)) != 0u) {
        if (s->turn_accumulator > s->movement_accumulator)
            s->movement_accumulator = s->turn_accumulator;
        else
            s->turn_accumulator = s->movement_accumulator;
        factor = sar(factor, 1);
    }
    if (factor < 256) factor = 256;
    if (factor > 768) factor = 768;
    if ((flags & DOK_INPUT_NATIVE_RUN) != 0u) factor *= 2;
    if ((flags & DOK_MOTION_SLOW) != 0u) factor = sar(factor, 1);
    if (s->movement_accumulator < 0) s->movement_accumulator = 0;
    value = mul32(mul32(s->angle_parameter, factor), s->turn_accumulator);
    s->turn_step = word(sar(value / 10000, 8));
    if (s->turn_step > 18) s->turn_step = 18;
    if ((flags & TURN_MASK) != 0u) {
        if ((flags & DOK_INPUT_NATIVE_TURN_POS) != 0u) {
            if (s->last_turn != 0) { s->turn_step = 1; s->last_turn = 0; }
            s->heading = word((int32_t)s->heading + s->turn_step);
            if (s->heading >= 360) s->heading = word((int32_t)s->heading - 360);
        }
        if ((flags & DOK_INPUT_NATIVE_TURN_NEG) != 0u) {
            if (s->last_turn == 0) { s->turn_step = 1; s->last_turn = 1; }
            s->heading = word((int32_t)s->heading - s->turn_step);
            if (s->heading < 0) s->heading = word((int32_t)s->heading + 360);
        }
    } else {
        s->turn_accumulator = 0;
        s->turn_step = 0;
    }
    value = mul32(sar(mul32(s->speed, factor), 8), s->movement_accumulator);
    s->linear_step = word(value / 10000);
    if (s->linear_step <= 0) { s->linear_step = 0; s->movement_accumulator = 0; }
    result.movement_flags = 0;
    if (s->linear_step != 0) {
        /* The original uses remembered flags except its forward-only fast
         * branch. New movement input has already replaced last_flags. */
        if ((flags & DOK_INPUT_NATIVE_FORWARD) == 0u ||
            (flags & (MOVE_MASK & ~DOK_INPUT_NATIVE_FORWARD)) != 0u)
            flags = s->last_flags;
        result.movement_flags = flags & MOVE_MASK;
        if ((flags & DOK_INPUT_NATIVE_FORWARD) != 0u) s->distance = -s->linear_step;
        if ((flags & DOK_INPUT_NATIVE_BACKWARD) != 0u) s->distance = s->linear_step;
        if ((flags & DOK_INPUT_NATIVE_STRAFE_LEFT) != 0u) s->distance = -s->linear_step;
        if ((flags & DOK_INPUT_NATIVE_STRAFE_RIGHT) != 0u) s->distance = s->linear_step;
    }
    result.factor = (uint32_t)factor;
    *out = result;
    return DOK_STATUS_OK;
}

static int index_for(int16_t heading, uint32_t strafe, uint32_t *out)
{
    int16_t index = word((int32_t)heading + (strafe != 0u ? 90 : 0));
    if (index >= 360) index = word((int32_t)index - 360);
    if (index < 0) index = word((int32_t)index + 360);
    if (index < 0 || index > 360) return 0;
    *out = (uint32_t)index;
    return 1;
}

dok_status dok_motion_update(dok_motion_state *state, uint32_t elapsed,
                             const dok_motion_services *services,
                             dok_motion_report *out)
{
    static const uint32_t directions[4] = {
        DOK_INPUT_NATIVE_FORWARD, DOK_INPUT_NATIVE_BACKWARD,
        DOK_INPUT_NATIVE_STRAFE_LEFT, DOK_INPUT_NATIVE_STRAFE_RIGHT
    };
    dok_motion_calculation calc;
    dok_motion_report report;
    int32_t sine[2] = {0,0}, cosine[2] = {0,0};
    uint32_t needed, i, index;
    if (state == NULL || services == NULL || services->trig == NULL ||
        services->move == NULL || out == NULL) return DOK_STATUS_INVALID_ARGUMENT;
    (void)dok_motion_calculate(state, elapsed, &calc);
    needed = ((calc.movement_flags & UINT32_C(0x30)) != 0u ? 1u : 0u) |
             ((calc.movement_flags & UINT32_C(0x180000)) != 0u ? 2u : 0u);
    for (i = 0; i < 2u; ++i) {
        if ((needed & (1u << i)) != 0u &&
            (!index_for(calc.state.heading, i, &index) ||
             !services->trig(services->trig_context,index,&sine[i],&cosine[i])))
            return DOK_STATUS_INVALID_STATE;
    }
    report.factor = calc.factor; report.movement_flags = calc.movement_flags;
    report.substeps = 0;
    for (i = 0; i < 4u; ++i) {
        uint32_t j, strafe = i >= 2u ? 1u : 0u;
        int16_t full = (i & 1u) != 0u ? calc.state.linear_step :
                       (int16_t)-calc.state.linear_step;
        if ((calc.movement_flags & directions[i]) == 0u) continue;
        for (j = 0; j < 3u; ++j) {
            dok_motion_substep request;
            int32_t ax, az;
            request.x=calc.state.x; request.y=calc.state.y; request.z=calc.state.z;
            request.distance = (int16_t)sar(full, 2);
            request.strafe = (uint16_t)strafe;
            request.ordinal = report.substeps++;
            request.candidate_x = add32(request.x,
                sar(mul32(sine[strafe], request.distance),15));
            request.candidate_z = add32(request.z,
                -sar(mul32(cosine[strafe], request.distance),15));
            ax=request.x; az=request.z;
            services->move(services->move_context,&request,&ax,&az);
            calc.state.x=ax; calc.state.z=az;
        }
        calc.state.distance=full;
    }
    report.moved = calc.state.x != state->x || calc.state.z != state->z;
    *state=calc.state; *out=report;
    return DOK_STATUS_OK;
}

void dok_motion_free_flight(void *context, const dok_motion_substep *request,
                            int32_t *accepted_x, int32_t *accepted_z)
{
    (void)context;
    *accepted_x=request->candidate_x; *accepted_z=request->candidate_z;
}

dok_status dok_motion_update_direct(dok_motion_state *state,uint32_t elapsed,
    const dok_motion_services *services,dok_motion_report *out)
{
    if(!state)return DOK_STATUS_INVALID_ARGUMENT;
    dok_motion_state direct=*state;
    direct.movement_accumulator=(direct.flags&MOVE_MASK)?10000:0;
    direct.turn_accumulator=(direct.flags&TURN_MASK)?10000:0;
    direct.last_flags=direct.flags;
    direct.last_turn=(direct.flags&DOK_INPUT_NATIVE_TURN_NEG)?1:0;
    dok_status result=dok_motion_update(&direct,elapsed,services,out);
    if(result==DOK_STATUS_OK)*state=direct;
    return result;
}
