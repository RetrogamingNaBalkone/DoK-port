#include "dok/motion.h"

#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include "render_trig.inc"

typedef struct trace { dok_motion_substep steps[DOK_MOTION_MAX_SUBSTEPS]; uint32_t count; } trace;
static int trig(void *context,uint32_t heading,int32_t *sine,int32_t *cosine)
{
    (void)context;
    if (heading>360u) return 0;
    *sine=dok_render_sine[heading]; *cosine=dok_render_cosine[heading]; return 1;
}
static void move(void *context,const dok_motion_substep *step,int32_t *x,int32_t *z)
{
    trace *t=context;
    if (t->count<DOK_MOTION_MAX_SUBSTEPS) t->steps[t->count++]=*step;
    dok_motion_free_flight(NULL,step,x,z);
}
static int32_t signed32(int64_t value)
{
    uint32_t bits=(uint32_t)(uint64_t)value;
    return bits<=(uint32_t)INT32_MAX?(int32_t)bits:
        (int32_t)((int64_t)bits-INT64_C(4294967296));
}
int main(void)
{
    int64_t v[16];
    for (;;) {
        dok_motion_state s; dok_motion_report report; trace t={0};
        dok_motion_services services={NULL,trig,&t,move};
        uint32_t i;
        int scan=scanf("%" SCNd64,&v[0]);
        if (scan==EOF) return 0;
        if (scan!=1) return 2;
        for (i=1;i<16u;++i) if (scanf("%" SCNd64,&v[i])!=1) return 2;
        (void)dok_motion_init(&s,signed32(v[2]),signed32(v[3]),signed32(v[4]),
                              (int16_t)v[5],(int16_t)v[6],signed32(v[7]));
        s.flags=(uint32_t)v[0]; s.movement_accumulator=signed32(v[8]);
        s.turn_accumulator=signed32(v[9]); s.last_turn=(int16_t)v[10];
        s.last_flags=(uint32_t)v[11]; s.distance=signed32(v[12]);
        s.turn_step=(int16_t)v[13];s.linear_step=(int16_t)v[14];s.accel_sign=(int16_t)v[15];
        if (dok_motion_update(&s,(uint32_t)v[1],&services,&report)!=DOK_STATUS_OK) {
            dok_motion_calculation calc;
            (void)dok_motion_calculate(&s,(uint32_t)v[1],&calc); s=calc.state;
            report.moved=0u; report.substeps=0u;
        }
        printf("%" PRIu32 " %" PRId32 " %" PRId32 " %" PRId32 " %d %d %" PRId32
               " %" PRId32 " %d %d %d %" PRId32 " %" PRId32 " %d %" PRIu32
               " %" PRIu32 " %" PRIu32,
               s.flags,s.x,s.y,s.z,(int)s.heading,(int)s.angle_parameter,s.speed,
               s.distance,(int)s.turn_step,(int)s.linear_step,(int)s.last_turn,
               s.movement_accumulator,s.turn_accumulator,(int)s.accel_sign,s.last_flags,
               report.moved,report.substeps);
        for (i=0;i<t.count;++i) {
            const dok_motion_substep *q=&t.steps[i];
            printf(" %d %u %" PRId32 " %" PRId32,(int)q->distance,
                   (unsigned)q->strafe,q->candidate_x,q->candidate_z);
        }
        putchar('\n');
    }
}
