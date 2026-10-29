#include <nds.h>
#include <fat.h>
#include <stdlib.h>
#include <string.h>
#include "../../tests/items_cases.h"
/* Read-only host observer: test inputs are loaded by actual ARM9 file I/O. */
_Alignas(32) volatile uint32_t g_nds_items_probe[16];
int main(void)
{
    dok_items_cases_workspace *w;dok_items_cases_report r={0};uint32_t failed=1;
    consoleDemoInit();
    if((uintptr_t)getHeapLimit()>DOK_NDS_ARM9_LIMIT)reduceHeapSize((uintptr_t)getHeapLimit()-DOK_NDS_ARM9_LIMIT);
    reduceHeapSize(DOK_NDS_RESERVE_BYTES);
    w=malloc(sizeof(*w));
    if(w&&fatInitDefault()){memset(w,0,sizeof(*w));failed=(uint32_t)dok_items_cases(w,&r,"fat:/dok/assets.ndp");}
    g_nds_items_probe[0]=0x14494b44u;g_nds_items_probe[1]=1;g_nds_items_probe[2]=failed?2u:1u;
    g_nds_items_probe[3]=r.checks;g_nds_items_probe[4]=r.failures;g_nds_items_probe[5]=r.line;
    g_nds_items_probe[6]=r.placements;g_nds_items_probe[7]=r.notifications;
    g_nds_items_probe[8]=sizeof(*w);g_nds_items_probe[9]=isDSiMode();
    g_nds_items_probe[10]=(uintptr_t)getHeapEnd();g_nds_items_probe[11]=(uintptr_t)getHeapLimit();
    DC_FlushRange((const void *)g_nds_items_probe,sizeof(g_nds_items_probe));
    for(;;){swiWaitForVBlank();++g_nds_items_probe[12];DC_FlushRange((const void *)g_nds_items_probe,sizeof(g_nds_items_probe));}
}
