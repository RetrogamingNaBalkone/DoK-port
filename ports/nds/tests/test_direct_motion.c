#include "dok/motion.h"
#include "dok/input.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
static int trig(void *p,uint32_t h,int32_t *s,int32_t *c)
{(void)p;(void)h;*s=0;*c=32767;return 1;}
int main(void)
{
    dok_motion_state s;dok_motion_report r;
    dok_motion_services services={0,trig,0,dok_motion_free_flight};
    assert(dok_motion_init(&s,0,0,0,0,5,120)==DOK_STATUS_OK);
    s.flags=DOK_INPUT_NATIVE_FORWARD;
    assert(dok_motion_update_direct(&s,416,&services,&r)==DOK_STATUS_OK);
    assert(s.z>0&&s.linear_step==120);
    int32_t z=s.z;s.flags=0;
    for(unsigned i=0;i<30;++i){
        assert(dok_motion_update_direct(&s,416,&services,&r)==DOK_STATUS_OK);
        assert(s.z==z&&!s.movement_accumulator&&!s.linear_step);
    }
    s.flags=DOK_INPUT_NATIVE_BACKWARD;
    assert(dok_motion_update_direct(&s,416,&services,&r)==DOK_STATUS_OK);
    assert(s.z<z&&s.linear_step==120);
    s.flags=DOK_INPUT_NATIVE_TURN_POS;
    assert(dok_motion_update_direct(&s,416,&services,&r)==DOK_STATUS_OK);
    assert(s.turn_step==5&&s.heading==5);
    s.flags=DOK_INPUT_NATIVE_TURN_NEG;
    assert(dok_motion_update_direct(&s,416,&services,&r)==DOK_STATUS_OK);
    assert(s.turn_step==5&&s.heading==0);
    s.flags=0;
    assert(dok_motion_update_direct(&s,416,&services,&r)==DOK_STATUS_OK);
    assert(s.heading==0&&!s.turn_step&&!s.turn_accumulator);
    puts("direct movement: start, release, reversal, turn reversal PASS");
    return 0;
}
