#include <nds.h>
#include <string.h>
#include "audio_ipc.h"
#include "audio_boot.inc"

/* BlocksDS 1.24 pinned SDK entry used by systemMsgHandler: copies the
 * 12-byte touchPosition (r0) and raw ARM7 key WORD (r1). Its ABI and effect
 * were checked in the actual SDK machine code; SDK scanKeys stays intact. */
extern void setTransferInputData(touchPosition *,uint16_t);
void __real_scanKeys(void);
static uint32_t consumed_sequence;
void __wrap_scanKeys(void)
{
    const volatile NdsInputMailbox *box=(const volatile NdsInputMailbox *)(uintptr_t)(DOK_AUDIO_ARM7_BOOT+96u);
    for(unsigned retry=0;retry<8u;++retry){
        DC_InvalidateRange((const void *)box,32);
        uint32_t before=box->sequence;
        if(!before || before==consumed_sequence)break;
        if(before&1u)continue;
        touchPosition touch;uint16_t keys;
        memcpy(&touch,(const void *)box->touch,12);keys=box->keys;
        __asm__ volatile("" ::: "memory");
        DC_InvalidateRange((const void *)box,32);
        if(before!=box->sequence)continue;
        uint32_t irq=enterCriticalSection();setTransferInputData(&touch,keys);
        consumed_sequence=before;leaveCriticalSection(irq);break;
    }
    __real_scanKeys();
}
