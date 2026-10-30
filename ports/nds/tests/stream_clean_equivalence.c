#include "stream_clean_equivalence.h"
#include <stdio.h>
#include <string.h>
extern void old_case(unsigned,unsigned,stream_result *);
extern void new_case(unsigned,unsigned,stream_result *);
static stream_result a,b,c;
int main(void)
{
    unsigned successful=0,refusals=0;
    for(unsigned n=0;n<4096;++n){
        old_case(n,0,&a);new_case(n,0,&b);
        if(memcmp(&a,&b,sizeof(a)))return 1;
        new_case(n,1,&c);
        if(c.fault||!c.prepare_state_unchanged)return 2;
        c.flushes=a.flushes;c.prepare_result=a.prepare_result;c.prepare_state_unchanged=a.prepare_state_unchanged;
        if(memcmp(&a,&c,sizeof(a)))return 3;
        successful+=a.result==DOK_NDS_RENDER_OK;refusals+=a.result!=DOK_NDS_RENDER_OK;
    }
    printf("{\"passed\":true,\"vectors\":4096,\"public_commit_cases\":4096,\"prepared_pair_cases\":4096,\"successful\":%u,\"refusals\":%u}\n",successful,refusals);
    return successful&&refusals?0:4;
}
