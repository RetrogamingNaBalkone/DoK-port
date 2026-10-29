#include <nds.h>
#include <fat.h>
#include <stdlib.h>
#include <string.h>
#include "../../tests/ai_cases.h"
_Alignas(32) volatile uint32_t g_nds_ai_probe[16];
_Alignas(32) volatile uint32_t g_nds_ai_profile[8];
static uint32_t counter(void){return cpuGetTiming();}
int main(void)
{
    dok_ai_cases_workspace *w;dok_ai_cases_report r={0};dok_combat_cases_report combat={0};uint32_t failed=1;
    consoleDemoInit();cpuStartTiming(0);
    if((uintptr_t)getHeapLimit()>DOK_NDS_ARM9_LIMIT)reduceHeapSize((uintptr_t)getHeapLimit()-DOK_NDS_ARM9_LIMIT);
    reduceHeapSize(DOK_NDS_RESERVE_BYTES);w=malloc(sizeof(*w));
    if(w&&fatInitDefault()){
        memset(w,0,sizeof(*w));w->profile_counter=counter;
        failed=(uint32_t)dok_combat_cases(&w->game,&combat,"fat:/dok/assets.ndp");
        if(!failed)failed=(uint32_t)dok_ai_cases(w,&r,"fat:/dok/assets.ndp");
        g_nds_ai_profile[0]=w->callback_ticks;g_nds_ai_profile[1]=w->max_callback_ticks;g_nds_ai_profile[2]=w->profile_samples;
        g_nds_ai_profile[3]=w->stress_ticks;g_nds_ai_profile[4]=w->max_stress_ticks;g_nds_ai_profile[5]=w->stress_samples;
    }
    g_nds_ai_profile[6]=combat.checks;g_nds_ai_profile[7]=combat.trace_hash;
    g_nds_ai_probe[0]=0x16414b44u;g_nds_ai_probe[1]=1;g_nds_ai_probe[2]=failed?2u:1u;
    g_nds_ai_probe[3]=r.checks;g_nds_ai_probe[4]=r.failures;g_nds_ai_probe[5]=r.line;
    g_nds_ai_probe[6]=r.scenarios;g_nds_ai_probe[7]=r.callbacks;g_nds_ai_probe[8]=r.trace_hash;
    g_nds_ai_probe[9]=isDSiMode();g_nds_ai_probe[10]=sizeof(*w);g_nds_ai_probe[11]=sizeof(w->ai);
    g_nds_ai_probe[14]=combat.line;g_nds_ai_probe[15]=r.stress_callbacks;
    DC_FlushRange((const void *)g_nds_ai_profile,sizeof(g_nds_ai_profile));DC_FlushRange((const void *)g_nds_ai_probe,sizeof(g_nds_ai_probe));
    for(;;){swiWaitForVBlank();++g_nds_ai_probe[12];DC_FlushRange((const void *)g_nds_ai_probe,sizeof(g_nds_ai_probe));}
}
