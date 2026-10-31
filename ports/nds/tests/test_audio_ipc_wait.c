/* Exercise the real ARM9 command path with independently scheduled ARM7 acks. */
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include "../platform/audio/arm9.c"
_Static_assert(offsetof(NdsAudioShared,status)==32&&sizeof(NdsAudioShared)==96,
    "ARM7 status must exclusively occupy two cache lines");
static unsigned frames,ack_frame,reject_send,immediate_ack,status_frame,ack_on_poll;
static uint32_t sent_serial;
static const void *flushed;
static size_t flushed_bytes;
uint16_t test_fifo_control;
int fifoCheckAddress(int channel)
{assert(channel==FIFO_USER_01);if(ack_on_poll)storage.status.command_ack=sent_serial;return 0;}
void DC_InvalidateRange(const void *p,size_t n) {assert(p==(const void *)&storage.status&&n==64);}
void DC_FlushRange(const void *p,size_t n) {flushed=p;flushed_bytes=n;}
void *memUncached(void *p) {return p;}
int fifoSendAddress(int channel,void *data)
{
    assert(channel==FIFO_USER_01&&data==&mailbox);
    assert(flushed==&mailbox&&flushed_bytes==sizeof(mailbox));
    NdsAudioCommand *m=(NdsAudioCommand *)(void *)data;
    sent_serial=m->serial;
    if(immediate_ack)storage.status.command_ack=sent_serial;
    return !reject_send;
}
void swiWaitForVBlank(void)
{
    ++frames;
    if(frames==ack_frame)storage.status.command_ack=sent_serial;
    if(frames==status_frame)storage.status.status_sequence=2;
}
static void reset(void)
{
    memset(&storage,0,sizeof(storage));shared=&storage;
    command_fault=0;
    frames=ack_frame=reject_send=immediate_ack=status_frame=ack_on_poll=0;
}
int main(void)
{
    NdsAudioCommand m={0};
    reset();immediate_ack=1;assert(command(&m)&&frames==0);
    reset();ack_frame=1;assert(command(&m)&&frames==1);
    reset();ack_frame=8;assert(command(&m)&&frames==8);
    reset();assert(!command(&m)&&frames==8);
    NdsAudioCommand saved=mailbox;
    m.op=NDS_AUDIO_STOP;assert(!command(&m)&&!memcmp(&saved,&mailbox,sizeof(saved)));
    storage.status.command_ack=sent_serial;
    assert(!command(&m)&&!memcmp(&saved,&mailbox,sizeof(saved)));
    reset();storage.status.command_ack=serial;assert(!command(&m)&&frames==8);
    reset();reject_send=1;assert(!command(&m)&&frames==0);
    reset();ack_on_poll=1;assert(command(&m)&&frames==1);
    reset();storage.status.ready=1;assert(nds_audio_status().ready&&frames==0);
    reset();storage.status.status_sequence=1;status_frame=1;
    assert(!nds_audio_status().stale_commands&&frames==1);
    reset();storage.status.status_sequence=1;status_frame=8;
    assert(!nds_audio_status().stale_commands&&frames==8);
    reset();storage.status.status_sequence=1;
    NdsAudioStatus failed=nds_audio_status();
    assert(failed.stale_commands==UINT32_MAX&&!failed.ready&&frames==8);
    reset();storage.status.status_sequence=1;storage.status.ready=1;
    storage.status.command_ack=123;test_fifo_control=0xabcd;
    uint32_t debug[8];nds_audio_debug(debug);
    assert(debug[0]==serial&&debug[1]==123&&debug[2]==1&&debug[3]==1);
    assert(debug[5]==123&&debug[6]==1&&debug[7]==0xabcd&&frames==0);
    puts("audio IPC: 6 ACK cases and 4 status cases PASS");
    return 0;
}
