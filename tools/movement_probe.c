#include "dok/movement.h"
#include <stdio.h>
#include <inttypes.h>

static int32_t signed32(uint32_t n)
{ return n<=INT32_MAX?(int32_t)n:-1-(int32_t)(UINT32_MAX-n); }
int main(void)
{
    uint32_t v[15];
    for(;;) {
        for(unsigned i=0;i<15;++i) if(scanf("%"SCNu32,&v[i])!=1) return i?1:0;
        dok_runtime_actor a={0};dok_motion m={v[12]};
        a.x=signed32(v[2]);a.y=signed32(v[3]);a.z=signed32(v[4]);
        a.heading=(int16_t)v[5];a.angle_34=(int16_t)v[6];a.base_speed=signed32(v[7]);
        a.accumulator_a=signed32(v[8]);a.accumulator_b=signed32(v[9]);
        a.command=(int16_t)v[10];a.motion_flags=v[11];a.step_distance=signed32(v[13]);
        int changed=v[14]?dok_motion_update(&a,&m,v[0],v[1],0,0):dok_motion_original(&a,&m,v[0],v[1],0,0);
        int32_t out[]={a.x,a.y,a.z,a.heading,a.direction,a.step,a.accumulator_a,
            a.accumulator_b,a.command,a.parameter_98,a.step_distance,signed32(a.motion_flags),signed32(m.remembered_flags),changed};
        for(unsigned i=0;i<14;++i) printf("%"PRId32"%c",out[i],i==13?'\n':' ');
    }
}
