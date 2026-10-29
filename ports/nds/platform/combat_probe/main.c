#include <nds.h>
#include <fat.h>
#include <stdlib.h>
#include <string.h>
#include "../../tests/combat_cases.h"
_Alignas(32) volatile uint32_t g_nds_combat_probe[16];
int main(void)
{
    dok_combat_cases_workspace *w;dok_combat_cases_report r={0};uint32_t failed=1;
    consoleDemoInit();
    if((uintptr_t)getHeapLimit()>DOK_NDS_ARM9_LIMIT)reduceHeapSize((uintptr_t)getHeapLimit()-DOK_NDS_ARM9_LIMIT);
    reduceHeapSize(DOK_NDS_RESERVE_BYTES);w=malloc(sizeof(*w));
    if(w&&fatInitDefault()){memset(w,0,sizeof(*w));failed=(uint32_t)dok_combat_cases(w,&r,"fat:/dok/assets.ndp");}
    g_nds_combat_probe[0]=0x15434b44u;g_nds_combat_probe[1]=1;g_nds_combat_probe[2]=failed?2u:1u;
    g_nds_combat_probe[3]=r.checks;g_nds_combat_probe[4]=r.failures;g_nds_combat_probe[5]=r.line;
    g_nds_combat_probe[6]=r.scenarios;g_nds_combat_probe[7]=r.callbacks;g_nds_combat_probe[8]=r.trace_hash;
    g_nds_combat_probe[9]=isDSiMode();g_nds_combat_probe[10]=sizeof(*w);g_nds_combat_probe[11]=sizeof(w->combat);
    DC_FlushRange((const void *)g_nds_combat_probe,sizeof(g_nds_combat_probe));
    for(;;){swiWaitForVBlank();++g_nds_combat_probe[12];DC_FlushRange((const void *)g_nds_combat_probe,sizeof(g_nds_combat_probe));}
}
