#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
static int ime;
static unsigned checks, enters, leaves;
static uint32_t raw;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
int enterCriticalSection(void){int previous=ime;ime=0;++enters;return previous;}
void leaveCriticalSection(int previous){CHECK(ime==0);ime=previous;++leaves;}
uint32_t __real_cpuGetTiming(void){CHECK(ime==0);return raw;}
uint32_t __wrap_cpuGetTiming(void);
int main(void)
{
    const uint32_t cases[]={0,1,0xffff,0x10000,0x1234fffe,0xffffffff,0};
    for(int initial=0;initial<=1;++initial)for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);++i){
        ime=initial;raw=cases[i];CHECK(__wrap_cpuGetTiming()==raw);CHECK(ime==initial);
    }
    CHECK(enters==14&&leaves==14);
    printf("{\"passed\":true,\"checks\":%u}\n",checks);
    return 0;
}
