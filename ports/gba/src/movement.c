#include "dok/movement.h"
#include "renderer_trig.inc"

static int32_t s32(uint32_t n)
{ return n<=INT32_MAX?(int32_t)n:-1-(int32_t)(UINT32_MAX-n); }
static int16_t s16(uint32_t n)
{ n&=65535;return n<=INT16_MAX?(int16_t)n:(int16_t)(-1-(int32_t)(65535-n)); }
static int32_t sar(int32_t n,unsigned shift)
{ uint32_t bits=(uint32_t)n;return s32((bits>>shift)|(n<0?UINT32_MAX<<(32-shift):0)); }
static int32_t mul(int32_t a,int32_t b) { return s32((uint32_t)a*(uint32_t)b); }
int16_t dok_motion_heading(int32_t heading)
{ int32_t n=heading%360;return (int16_t)(n<0?n+360:n); }
void dok_input_sample(dok_input *in,uint16_t keys)
{
    in->held=keys&1023u;in->pressed=in->held&~in->previous;
    in->released=in->previous&~in->held;in->previous=in->held;
}
uint32_t dok_input_commands(const dok_input *in)
{
    uint32_t f=0,k=in->held;
    if(k&0x40) f|=DOK_INPUT_FORWARD;
    if(k&0x80) f|=DOK_INPUT_BACK;
    if(k&0x20) f|=DOK_INPUT_LEFT;
    if(k&0x10) f|=DOK_INPUT_RIGHT;
    if(k&0x200) f|=DOK_INPUT_STRAFE_LEFT;
    if(k&0x100) f|=DOK_INPUT_STRAFE_RIGHT;
    if(k&2) f|=DOK_INPUT_RUN;
    if(k&1) f|=DOK_INPUT_FIRE;
    if(in->pressed&8) f|=DOK_INPUT_USE;
    return f;
}
void dok_motion_free(dok_runtime_actor *a,int16_t distance,int strafe,void *context)
{
    (void)context;
    unsigned angle=(unsigned)dok_motion_heading((int32_t)a->heading+(strafe?90:0));
    int32_t dx=sar(mul(trig[angle],distance),15);
    int32_t dz=sar(mul(trig[361+angle],distance),15);
    a->x=s32((uint32_t)a->x+(uint32_t)dx);
    a->z=s32((uint32_t)a->z-(uint32_t)dz);
}
static void translate(dok_runtime_actor *a,int16_t distance,int strafe,
    dok_motion_move move,void *context)
{
    int16_t quarter=(int16_t)sar(distance,2);
    a->step_distance=quarter;
    for(unsigned i=0;i<3;++i) move(a,quarter,strafe,context);
    a->step_distance=distance;
}
int dok_motion_original(dok_runtime_actor *a,dok_motion *m,uint32_t f,uint32_t dt,
    dok_motion_move move,void *context)
{
    int32_t old_x=a->x,old_z=a->z;
    f|=a->motion_flags;
    a->motion_flags=f&UINT32_C(0xfbe7dfc3);
    if(f&(DOK_INPUT_FORWARD|DOK_INPUT_BACK|DOK_INPUT_STRAFE_LEFT|DOK_INPUT_STRAFE_RIGHT)) {
        a->parameter_98=1;m->remembered_flags=f;
    } else a->parameter_98=-1;
    a->accumulator_a=s32((uint32_t)a->accumulator_a+(uint32_t)a->parameter_98*dt);
    a->accumulator_b=s32((uint32_t)a->accumulator_b+dt);
    if(a->accumulator_a>10000) a->accumulator_a=10000;
    if(a->accumulator_a<0) a->accumulator_a=0;
    if(a->accumulator_b>10000) a->accumulator_b=10000;
    int32_t factor=s32((dt<<8)/416u);
    if((f&(DOK_INPUT_LEFT|DOK_INPUT_RIGHT)) && (f&(DOK_INPUT_FORWARD|DOK_INPUT_BACK))) {
        if(a->accumulator_a<a->accumulator_b) a->accumulator_a=a->accumulator_b;
        else a->accumulator_b=a->accumulator_a;
        factor=sar(factor,1);
    }
    if(factor<256) factor=256;
    if(factor>768) factor=768;
    if(f&DOK_INPUT_RUN) factor=mul(factor,2);
    if(f&DOK_INPUT_SLOW) factor=sar(factor,1);
    a->direction=s16((uint32_t)sar(mul(mul(a->angle_34,factor),a->accumulator_b)/10000,8));
    if(a->direction>18) a->direction=18;
    if(!(f&(DOK_INPUT_LEFT|DOK_INPUT_RIGHT))) {a->accumulator_b=0;a->direction=0;}
    else {
        if(f&DOK_INPUT_LEFT) {
            if(a->command) {a->direction=1;a->command=0;}
            a->heading=s16((uint32_t)((int32_t)a->heading+a->direction));
            if(a->heading>=360) a->heading=s16((uint32_t)((int32_t)a->heading-360));
        }
        if(f&DOK_INPUT_RIGHT) {
            if(!a->command) {a->direction=1;a->command=1;}
            a->heading=s16((uint32_t)((int32_t)a->heading-a->direction));
            if(a->heading<0) a->heading=s16((uint32_t)((int32_t)a->heading+360));
        }
    }
    a->step=s16((uint32_t)(mul(sar(mul(a->base_speed,factor),8),a->accumulator_a)/10000));
    if(a->step<=0) {a->step=0;a->accumulator_a=0;}
    if(a->step) {
        if(!(f&DOK_INPUT_FORWARD) || (f&(DOK_INPUT_BACK|DOK_INPUT_STRAFE_LEFT|DOK_INPUT_STRAFE_RIGHT)))
            f=m->remembered_flags;
        if(!move) move=dok_motion_free;
        if(f&DOK_INPUT_FORWARD) translate(a,(int16_t)-a->step,0,move,context);
        if(f&DOK_INPUT_BACK) translate(a,a->step,0,move,context);
        if(f&DOK_INPUT_STRAFE_LEFT) translate(a,(int16_t)-a->step,1,move,context);
        if(f&DOK_INPUT_STRAFE_RIGHT) translate(a,a->step,1,move,context);
    }
    return a->x!=old_x || a->z!=old_z;
}
int dok_motion_update(dok_runtime_actor *a,dok_motion *m,uint32_t f,uint32_t dt,
    dok_motion_move move,void *context)
{
    const uint32_t translation=DOK_INPUT_FORWARD|DOK_INPUT_BACK|DOK_INPUT_STRAFE_LEFT|DOK_INPUT_STRAFE_RIGHT;
    /* Opposing commands are neutral on each axis. No remembered release coast.
     * Turn/move accumulator synchronization must not restart translation. */
    if((f&(DOK_INPUT_FORWARD|DOK_INPUT_BACK))==(DOK_INPUT_FORWARD|DOK_INPUT_BACK))
        f&=~(DOK_INPUT_FORWARD|DOK_INPUT_BACK);
    if((f&(DOK_INPUT_STRAFE_LEFT|DOK_INPUT_STRAFE_RIGHT))==(DOK_INPUT_STRAFE_LEFT|DOK_INPUT_STRAFE_RIGHT))
        f&=~(DOK_INPUT_STRAFE_LEFT|DOK_INPUT_STRAFE_RIGHT);
    a->accumulator_a=(f&translation)?10000:0;
    m->remembered_flags=f&translation?f:0;
    return dok_motion_original(a,m,f,dt,move,context);
}
