#include "motion_cases.h"
#include "dok/input.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>
#include "render_trig.inc"

typedef struct capture {
    dok_motion_substep steps[DOK_MOTION_MAX_SUBSTEPS];
    uint32_t count, reject, bad_trig, trig_calls;
} capture;

#define CHECK(r,condition) do { \
    ++(r)->checks; \
    if (!(condition)) { ++(r)->failures; \
        if ((r)->first_failed_line==0u) (r)->first_failed_line=(uint32_t)__LINE__; \
        return; \
    } \
} while (0)

static int trig(void *context,uint32_t heading,int32_t *sine,int32_t *cosine)
{
    capture *c=context;
    ++c->trig_calls;
    if (c->bad_trig!=0u || heading>360u) return 0;
    *sine=dok_render_sine[heading]; *cosine=dok_render_cosine[heading]; return 1;
}
static void move(void *context,const dok_motion_substep *step,int32_t *x,int32_t *z)
{
    capture *c=context;
    if (c->count<DOK_MOTION_MAX_SUBSTEPS) c->steps[c->count]=*step;
    ++c->count;
    if (c->reject==0u) dok_motion_free_flight(NULL,step,x,z);
}
static dok_motion_services services(capture *c)
{
    dok_motion_services result={c,trig,c,move}; return result;
}

static void native_examples(dok_motion_test_report *r)
{
    capture c={0}; dok_motion_services svc=services(&c);
    dok_motion_state s,before; dok_motion_report report,sentinel={9,8,7,6};
    dok_motion_calculation calc;
    ++r->motion_cases;
    CHECK(r,dok_motion_init(&s,1000,-300,1000,90,5,120)==DOK_STATUS_OK);
    CHECK(r,s.accel_sign==1 && s.last_flags==0u && s.distance==0);
    s.flags=DOK_INPUT_NATIVE_FORWARD;
    CHECK(r,dok_motion_update(&s,0u,&svc,&report)==DOK_STATUS_OK);
    CHECK(r,s.x==1000 && s.movement_accumulator==0 && report.substeps==0u);
    s.flags|=DOK_INPUT_NATIVE_FORWARD;
    CHECK(r,dok_motion_update(&s,416u,&svc,&report)==DOK_STATUS_OK);
    CHECK(r,s.x==997 && s.z==1000 && s.y==-300 && s.distance==-4);
    CHECK(r,s.linear_step==4 && s.movement_accumulator==416 && c.count==3u);
    CHECK(r,c.steps[0].distance==-1 && c.steps[2].ordinal==2u);
    CHECK(r,report.factor==256u && report.moved==1u && s.flags==0u);
    s.movement_accumulator=10000;s.flags=0u;s.last_flags=DOK_INPUT_NATIVE_FORWARD;
    s.x=1000;c.count=0;
    CHECK(r,dok_motion_update(&s,416u,&svc,&report)==DOK_STATUS_OK);
    CHECK(r,s.x==913 && s.linear_step==115 && s.distance==-115);
    CHECK(r,s.movement_accumulator==9584 && s.accel_sign==-1);
    s.x=1000;s.flags=DOK_INPUT_NATIVE_FORWARD|DOK_INPUT_NATIVE_BACKWARD;
    s.movement_accumulator=500;c.count=0;
    CHECK(r,dok_motion_update(&s,0u,&svc,&report)==DOK_STATUS_OK);
    CHECK(r,s.x==997 && s.distance==6 && report.substeps==6u);
    CHECK(r,c.steps[2].distance==-2 && c.steps[3].distance==1);
    s.flags=DOK_INPUT_NATIVE_TURN_POS;s.heading=90;s.turn_accumulator=0;s.last_turn=0;
    s.movement_accumulator=0;
    CHECK(r,dok_motion_update(&s,416u,&svc,&report)==DOK_STATUS_OK);
    CHECK(r,s.heading==90 && s.turn_step==0);
    s.flags=DOK_INPUT_NATIVE_TURN_NEG;s.heading=90;s.turn_accumulator=0;s.last_turn=0;
    CHECK(r,dok_motion_update(&s,416u,&svc,&report)==DOK_STATUS_OK);
    CHECK(r,s.heading==89 && s.turn_step==1 && s.last_turn==1);
    s.flags=DOK_INPUT_NATIVE_TURN_POS|DOK_INPUT_NATIVE_TURN_NEG;
    s.heading=90;s.turn_accumulator=10000;s.last_turn=0;
    CHECK(r,dok_motion_update(&s,416u,&svc,&report)==DOK_STATUS_OK);
    CHECK(r,s.heading==94 && s.turn_step==1 && s.last_turn==1);
    s.flags=DOK_INPUT_NATIVE_FORWARD|DOK_INPUT_NATIVE_RUN|DOK_MOTION_SLOW;
    s.movement_accumulator=10000;s.turn_accumulator=0;s.heading=90;s.x=1000;
    CHECK(r,dok_motion_update(&s,416u,&svc,&report)==DOK_STATUS_OK);
    CHECK(r,report.factor==256u && s.x==910 && s.distance==-120);
    s.flags=DOK_INPUT_NATIVE_FORWARD;s.x=1000;c.reject=1u;c.count=0u;
    CHECK(r,dok_motion_update(&s,416u,&svc,&report)==DOK_STATUS_OK);
    CHECK(r,s.x==1000 && report.moved==0u && report.substeps==3u && c.count==3u);
    s.flags=DOK_INPUT_NATIVE_FORWARD;s.heading=2000;before=s;report=sentinel;c.count=0u;
    CHECK(r,dok_motion_calculate(&s,416u,&calc)==DOK_STATUS_OK);
    CHECK(r,calc.state.heading==2000 && calc.movement_flags==DOK_INPUT_NATIVE_FORWARD);
    CHECK(r,dok_motion_update(&s,416u,&svc,&report)==DOK_STATUS_INVALID_STATE);
    CHECK(r,memcmp(&s,&before,sizeof(s))==0 && memcmp(&report,&sentinel,sizeof(report))==0);
    CHECK(r,c.count==0u);
    s.heading=90;before=s;c.bad_trig=1u;
    CHECK(r,dok_motion_update(&s,416u,&svc,&report)==DOK_STATUS_INVALID_STATE);
    CHECK(r,memcmp(&s,&before,sizeof(s))==0 && c.count==0u);
}

static void directions(dok_motion_test_report *r)
{
    uint32_t h;
    ++r->motion_cases;
    for (h=0u;h<360u;++h) {
        capture c={0};dok_motion_services svc=services(&c);
        dok_motion_state s;dok_motion_report report;
        uint32_t i;
        (void)dok_motion_init(&s,10000,-300,10000,(int16_t)h,5,120);
        s.flags=UINT32_C(0x180030);s.movement_accumulator=10000;
        CHECK(r,dok_motion_update(&s,416u,&svc,&report)==DOK_STATUS_OK);
        CHECK(r,report.substeps==12u && c.count==12u && c.trig_calls==2u);
        CHECK(r,s.y==-300 && s.distance==120 && s.heading==(int16_t)h);
        for (i=0u;i<12u;++i) {
            CHECK(r,c.steps[i].ordinal==i && c.steps[i].strafe==(i>=6u?1u:0u));
            CHECK(r,c.steps[i].distance==((i/3u)%2u!=0u?30:-30));
            if (i!=0u) CHECK(r,c.steps[i].x==c.steps[i-1u].candidate_x &&
                               c.steps[i].z==c.steps[i-1u].candidate_z);
        }
    }
}

static void camera_examples(dok_motion_test_report *r)
{
    dok_camera_history h,before;dok_camera_pose out,sentinel={7,8,9,10};
    dok_motion_state s;
    uint32_t i;
    ++r->camera_cases;
    dok_camera_reset(&h);
    CHECK(r,dok_camera_predict(&h,0u,416u,&out)==DOK_STATUS_INVALID_STATE);
    (void)dok_motion_init(&s,0,-300,1000,359,5,120);
    CHECK(r,dok_camera_commit(&h,&s,UINT32_MAX-99u)==DOK_STATUS_OK);
    CHECK(r,dok_camera_predict(&h,UINT32_MAX,416u,&out)==DOK_STATUS_OK);
    CHECK(r,out.x==0 && out.heading_q16==359u*65536u);
    s.x=100;s.heading=1;
    CHECK(r,dok_camera_commit(&h,&s,100u)==DOK_STATUS_OK);
    before=h;
    CHECK(r,dok_camera_predict(&h,200u,416u,&out)==DOK_STATUS_OK);
    CHECK(r,out.x==150 && out.heading_q16==2u*65536u && dok_camera_heading(&out)==2u);
    CHECK(r,dok_camera_interpolate(&h,32768u,&out)==DOK_STATUS_OK);
    CHECK(r,out.x==50 && out.heading_q16==0u);
    CHECK(r,dok_camera_predict(&h,10000u,416u,&out)==DOK_STATUS_OK);
    CHECK(r,out.x==308 && out.heading_q16==338165u);
    CHECK(r,memcmp(&h,&before,sizeof(h))==0);
    out=sentinel;
    CHECK(r,dok_camera_predict(&h,99u,416u,&out)==DOK_STATUS_INVALID_STATE);
    CHECK(r,memcmp(&out,&sentinel,sizeof(out))==0);
    CHECK(r,dok_camera_predict(&h,200u,417u,&out)==DOK_STATUS_INVALID_ARGUMENT);
    CHECK(r,dok_camera_interpolate(&h,65537u,&out)==DOK_STATUS_INVALID_ARGUMENT);
    CHECK(r,dok_camera_commit(&h,&s,99u)==DOK_STATUS_INVALID_STATE);
    CHECK(r,memcmp(&h,&before,sizeof(h))==0);
    s.heading=-1;
    CHECK(r,dok_camera_commit(&h,&s,101u)==DOK_STATUS_INVALID_STATE);
    s.heading=90;s.x=700;
    CHECK(r,dok_camera_commit(&h,&s,100u)==DOK_STATUS_OK);
    CHECK(r,h.count==1u && dok_camera_predict(&h,500u,416u,&out)==DOK_STATUS_OK);
    CHECK(r,out.x==700 && out.heading_q16==90u*65536u);
    s.heading=360;
    CHECK(r,dok_camera_commit(&h,&s,101u)==DOK_STATUS_OK);
    CHECK(r,s.heading==360 && h.current.pose.heading_q16==0u);
    dok_camera_reset(&h);s.x=INT32_MAX-1;
    CHECK(r,dok_camera_commit(&h,&s,0u)==DOK_STATUS_OK);
    s.x=INT32_MAX;
    CHECK(r,dok_camera_commit(&h,&s,1u)==DOK_STATUS_OK);
    out=sentinel;
    CHECK(r,dok_camera_predict(&h,3u,416u,&out)==DOK_STATUS_LIMIT_REACHED);
    CHECK(r,memcmp(&out,&sentinel,sizeof(out))==0);
    for (i=0;i<360u;++i) {
        dok_camera_reset(&h);s.heading=(int16_t)i;s.x=0;
        CHECK(r,dok_camera_commit(&h,&s,0u)==DOK_STATUS_OK);
        s.heading=(int16_t)((i+1u)%360u);s.x=10;
        CHECK(r,dok_camera_commit(&h,&s,416u)==DOK_STATUS_OK);
        CHECK(r,dok_camera_predict(&h,624u,416u,&out)==DOK_STATUS_OK);
        CHECK(r,out.x==15 && out.heading_q16==((i*65536u+98304u)%23592960u));
        CHECK(r,dok_camera_heading(&out)==((i+2u)%360u));
    }
}

static void observation_separation(dok_motion_test_report *r)
{
    dok_motion_state final[3];
    uint32_t mode;
    ++r->camera_cases;
    for (mode=0u;mode<3u;++mode) {
        capture c={0};dok_motion_services svc=services(&c);
        dok_motion_state s;dok_motion_report report;
        dok_camera_history h;
        uint32_t tick=0u,n;
        (void)dok_motion_init(&s,15000,-300,15000,90,5,120);
        dok_camera_reset(&h);
        CHECK(r,dok_camera_commit(&h,&s,tick)==DOK_STATUS_OK);
        for (n=0u;n<144u;++n) {
            uint32_t elapsed=416u+(n%5u)*7u,j;
            s.flags=(n%3u==0u?DOK_INPUT_NATIVE_FORWARD:DOK_INPUT_NATIVE_STRAFE_LEFT)|
                    (n%7u<3u?DOK_INPUT_NATIVE_TURN_POS:0u)|
                    (n%11u<4u?DOK_INPUT_NATIVE_RUN:0u);
            c.count=0u;
            CHECK(r,dok_motion_update(&s,elapsed,&svc,&report)==DOK_STATUS_OK);
            tick+=elapsed;
            CHECK(r,dok_camera_commit(&h,&s,tick)==DOK_STATUS_OK);
            for (j=0u;j<(mode==0u?0u:mode==1u?2u:4u);++j) {
                dok_camera_history saved=h;dok_motion_state saved_s=s;
                dok_camera_pose pose;
                uint32_t callbacks=c.count;
                CHECK(r,dok_camera_predict(&h,tick+j*100u,416u,&pose)==DOK_STATUS_OK);
                CHECK(r,memcmp(&s,&saved_s,sizeof(s))==0 && memcmp(&h,&saved,sizeof(h))==0);
                CHECK(r,c.count==callbacks);
                ++r->prediction_observations;
            }
        }
        final[mode]=s;
    }
    CHECK(r,memcmp(&final[0],&final[1],sizeof(final[0]))==0);
    CHECK(r,memcmp(&final[0],&final[2],sizeof(final[0]))==0);
}

int dok_motion_run_cases(dok_motion_test_report *report)
{
    if (report==NULL) return 1;
    memset(report,0,sizeof(*report));
    native_examples(report);directions(report);camera_examples(report);observation_separation(report);
    return report->failures!=0u;
}
