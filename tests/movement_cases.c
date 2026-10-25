#include "dok/movement.h"
#define CHECK(x) do { if(!(x)) return __LINE__; } while(0)
static dok_runtime_actor initial(void)
{
    dok_runtime_actor a={0};a.x=15120;a.y=-300;a.z=22800;a.heading=90;
    a.angle_34=5;a.base_speed=120;a.bound_9e=-274;a.bound_a0=-326;return a;
}
unsigned dok_movement_cases(void)
{
    dok_runtime_actor a=initial(),b;dok_motion m={0},n;dok_input in={0};
    CHECK(dok_motion_heading(-1)==359 && dok_motion_heading(INT32_MIN)==232);
    CHECK(dok_motion_heading(INT32_MAX)==127 && dok_motion_heading(360)==0);
    dok_input_sample(&in,0x48);CHECK(in.held==0x48 && in.pressed==0x48 && !in.released);
    CHECK(dok_input_commands(&in)==(DOK_INPUT_FORWARD|DOK_INPUT_USE));
    dok_input_sample(&in,0x48);CHECK(!in.pressed && !in.released);
    CHECK(dok_input_commands(&in)==DOK_INPUT_FORWARD);
    dok_input_sample(&in,0);CHECK(in.released==0x48 && !in.held);
    for(unsigned k=0;k<1024;++k) {
        dok_input sample={0};dok_input_sample(&sample,(uint16_t)k);
        uint32_t f=dok_input_commands(&sample);
        CHECK(!!(f&DOK_INPUT_FORWARD)==!!(k&64));
        CHECK(!!(f&DOK_INPUT_STRAFE_LEFT)==!!(k&512));
        CHECK(!!(f&DOK_INPUT_STRAFE_RIGHT)==!!(k&256));
        CHECK(!!(f&DOK_INPUT_FIRE)==!!(k&1));
        CHECK(!!(f&DOK_INPUT_RUN)==!!(k&2));
    }
    dok_motion_update(&a,&m,DOK_INPUT_FORWARD,416,0,0);
    CHECK(a.x==15030 && a.z==22800 && a.y==-300 && a.step==120);
    b=a;
    for(unsigned i=0;i<100;++i) dok_motion_update(&a,&m,0,416,0,0);
    CHECK(a.x==b.x && a.z==b.z && a.step==0 && a.accumulator_a==0);
    dok_motion_update(&a,&m,DOK_INPUT_BACK,416,0,0);CHECK(a.x==15120 && a.z==22800);
    b=a;dok_motion_update(&a,&m,DOK_INPUT_FORWARD|DOK_INPUT_BACK,416,0,0);
    CHECK(a.x==b.x && a.z==b.z && !a.step);
    dok_motion_update(&a,&m,DOK_INPUT_STRAFE_LEFT,416,0,0);CHECK(a.z==22710);
    dok_motion_update(&a,&m,DOK_INPUT_STRAFE_RIGHT,416,0,0);CHECK(a.z==22800);
    CHECK(a.bound_9e==-274 && a.bound_a0==-326);
    a=initial();a.heading=359;a.accumulator_b=10000;
    dok_motion_update(&a,&m,DOK_INPUT_LEFT,416,0,0);CHECK(a.heading==4);
    a.heading=0;a.command=1;a.accumulator_b=10000;
    dok_motion_update(&a,&m,DOK_INPUT_RIGHT,416,0,0);CHECK(a.heading==355);
    a=initial();a.x=INT32_MIN+1;dok_motion_free(&a,-30,0,0);CHECK(a.x==INT32_MAX-28);
    a=initial();a.accumulator_a=10000;m.remembered_flags=DOK_INPUT_FORWARD;
    dok_motion_original(&a,&m,0,416,0,0);CHECK(a.x<15120); /* evidence: DOS coast */
    a=initial();b=a;m.remembered_flags=0;n=m;
    for(unsigned i=0;i<1000;++i) {
        uint32_t f=i%50<25?DOK_INPUT_FORWARD|DOK_INPUT_LEFT:DOK_INPUT_BACK|DOK_INPUT_RUN;
        dok_motion_update(&a,&m,f,416,0,0);dok_motion_update(&b,&n,f,416,0,0);
        CHECK(a.x==b.x && a.y==b.y && a.z==b.z && a.heading==b.heading);
        CHECK(a.accumulator_a==b.accumulator_a && a.accumulator_b==b.accumulator_b);
        CHECK(a.step==b.step && a.direction==b.direction && m.remembered_flags==n.remembered_flags);
    }
    return 0;
}
