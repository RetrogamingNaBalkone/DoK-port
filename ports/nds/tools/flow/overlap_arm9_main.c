#include <nds.h>
#include <fat.h>
#include <stdint.h>
#include <string.h>
#include "dok/memory_budget.h"

int nds_overlap_test_main(int argc,char **argv);
void nds_overlap_fixture_metrics(uint32_t out[16]);
_Alignas(32) volatile uint32_t g_nds_overlap_probe[32];
static void publish(uint32_t status,uint32_t line)
{
    uint32_t sequence=(g_nds_overlap_probe[23]+1u)|1u;
    g_nds_overlap_probe[23]=sequence;
    DC_FlushRange((const void *)g_nds_overlap_probe,sizeof(g_nds_overlap_probe));
    uint32_t actual[16];nds_overlap_fixture_metrics(actual);
    uint32_t result[32]={0x224f4b44u,1,status,line};
    memcpy(result+4,actual,sizeof(actual));
    result[20]=isDSiMode();result[21]=(uint32_t)(uintptr_t)getHeapLimit();
    result[23]=sequence;
    memcpy((void *)g_nds_overlap_probe,result,sizeof(result));
    DC_FlushRange((const void *)g_nds_overlap_probe,sizeof(g_nds_overlap_probe));
    g_nds_overlap_probe[23]=sequence+1u;
    DC_FlushRange((const void *)g_nds_overlap_probe,sizeof(g_nds_overlap_probe));
}
void nds_overlap_fixture_fail(uint32_t line)
{
    publish(3,line);
    for(;;)swiWaitForVBlank();
}
int main(void)
{
    consoleDemoInit();
    if((uintptr_t)getHeapLimit()>DOK_NDS_ARM9_LIMIT)
        reduceHeapSize((uintptr_t)getHeapLimit()-DOK_NDS_ARM9_LIMIT);
    reduceHeapSize(DOK_NDS_RESERVE_BYTES);
    publish(1,0);
    if(!fatInitDefault())nds_overlap_fixture_fail(__LINE__);
    char *args[]={"overlap-fixture","fat:/dok/assets.ndp"};
    if(nds_overlap_test_main(2,args))nds_overlap_fixture_fail(__LINE__);
    publish(2,0);
    for(;;){swiWaitForVBlank();++g_nds_overlap_probe[22];
        DC_FlushRange((const void *)g_nds_overlap_probe,sizeof(g_nds_overlap_probe));}
}
