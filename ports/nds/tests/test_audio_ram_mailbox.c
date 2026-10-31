/* Run the actual ARM9 RAM transport with a cache-publication consumer. */
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#define NDS_EMBEDDED_RESOURCES 1
_Alignas(32) uint32_t test_boot[24];
#include "../platform/audio/arm9.c"
static unsigned frames,ack_frame,immediate,publications,control_flushes;
static uint32_t published_serial;
static NdsAudioCommand visible;
uint16_t test_fifo_control;
void *memUncached(void *p) {return p;}
void DC_InvalidateRange(const void *p,size_t n)
{assert(n==64&&(p==(const void *)&storage.status||p==test_boot));}
void DC_FlushRange(const void *p,size_t n)
{
    if(p==&mailbox){assert(n==sizeof(mailbox));visible=mailbox;return;}
    assert(p==&test_boot[16]&&n==32);++control_flushes;
    const NdsAudioControl *c=p;
    if(c->serial&&c->serial!=published_serial){
        assert(c->magic==NDS_AUDIO_CONTROL_MAGIC);
        assert(c->address==(uint32_t)(uintptr_t)&mailbox);
        assert(c->serial==visible.serial&&visible.magic==NDS_AUDIO_IPC_MAGIC);
        published_serial=c->serial;++publications;
        if(immediate)storage.status.command_ack=published_serial;
    }
}
int fifoSendAddress(int channel,void *p) {(void)channel;(void)p;assert(0);return 0;}
int fifoCheckAddress(int channel) {(void)channel;assert(0);return 0;}
void swiWaitForVBlank(void)
{++frames;if(frames==ack_frame)storage.status.command_ack=published_serial;}
static void reset(void)
{
    memset(test_boot,0,sizeof(test_boot));memset(&storage,0,sizeof(storage));
    shared=&storage;command_fault=0;frames=ack_frame=immediate=publications=control_flushes=0;
    published_serial=0;
}
int main(void)
{
    NdsAudioCommand m={.op=NDS_AUDIO_ATTACH};
    reset();immediate=1;assert(command(&m)&&frames==0&&publications==1&&control_flushes==2);
    m.op=NDS_AUDIO_STOP;assert(command(&m)&&visible.op==NDS_AUDIO_STOP&&publications==2);
    reset();ack_frame=1;assert(command(&m)&&frames==1);
    reset();ack_frame=8;assert(command(&m)&&frames==8);
    reset();assert(!command(&m)&&frames==8);
    NdsAudioCommand saved=mailbox;uint32_t control[8];memcpy(control,&test_boot[16],32);
    storage.status.command_ack=published_serial;
    assert(!command(&m)&&!memcmp(&saved,&mailbox,sizeof(saved))&&!memcmp(control,&test_boot[16],32));
    reset();storage.status.command_ack=serial;assert(!command(&m)&&frames==8);
    puts("RAM audio mailbox: publication order, immediate/delayed/stale/missing ACK, timeout lifetime; no FIFO calls PASS");
    return 0;
}
