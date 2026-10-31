#include <nds.h>
#include <string.h>
#include "arm9.h"
#if NDS_EMBEDDED_RESOURCES
#include "audio_boot.inc"
#endif
void nds_audio_boot_debug(uint32_t out[32])
{
    memset(out,0,32u*sizeof(*out));
#if NDS_EMBEDDED_RESOURCES
    const volatile uint32_t *p=(const volatile uint32_t *)(uintptr_t)DOK_AUDIO_ARM7_BOOT;
    DC_InvalidateRange((const void *)p,64);
    DC_InvalidateRange((const void *)(p+24),32);
    for(unsigned i=0;i<32u;++i)out[i]=p[i];
#endif
}
static NdsAudioShared storage __attribute__((aligned(32)));
static volatile NdsAudioShared *shared;
static int8_t *ring;
static uint32_t serial;
static unsigned prefilled;
static int resident_active;
static NdsAudioCommand mailbox __attribute__((aligned(32)));
static int command_fault;
static void (*loading)(unsigned);
void nds_audio_set_loading(void (*progress)(unsigned)) { loading=progress; }
void nds_audio_debug(uint32_t out[8])
{
    memset(out,0,8u*sizeof(*out));out[0]=serial;
    if(shared){out[1]=shared->status.command_ack;out[2]=shared->status.ready;
        out[3]=shared->status.status_sequence;out[4]=(uint32_t)(uintptr_t)shared;}
    /* ARM7 exclusively owns these two aligned cache lines. ARM9 never
     * dirties them after attach; invalidation cannot discard ARM9 writes. */
    DC_InvalidateRange((const void *)&storage.status,64);
    out[5]=storage.status.command_ack;out[6]=storage.status.ready;
    out[7]=REG_IPC_FIFO_CR;
}

NdsAudioStatus nds_audio_status(void)
{
    NdsAudioStatus result = {0};
    if (shared) {
        /* A dead/interrupted writer must not trap the error path forever.
         * Yield only after the fast path fails, then report an unusable peer. */
        for (unsigned frame=0;frame<=8u;++frame) {
            for (unsigned retry=0;retry<4096u;++retry) {
                uint32_t before=shared->status.status_sequence;
                result=shared->status;
                uint32_t after=shared->status.status_sequence;
                if (!(before&1u)&&before==after) return result;
            }
            if(frame<8u)swiWaitForVBlank();
        }
        memset(&result,0,sizeof(result));
        result.stale_commands=UINT32_MAX; /* No coherent peer status. */
    }
    return result;
}
static int command(NdsAudioCommand *message)
{
    if(command_fault)return 0;
    unsigned timeout = 200000;
    message->magic = NDS_AUDIO_IPC_MAGIC;
    message->serial = ++serial;
    /* One in-flight immutable command; the peer copies it before ACK. A
     * timeout latches failure so a late receiver never sees an overwritten
     * command. Only one address word crosses the hardware FIFO. */
    memcpy(&mailbox,message,sizeof(mailbox));
    DC_FlushRange(&mailbox,sizeof(mailbox));
    if(loading)loading(56);
#if NDS_EMBEDDED_RESOURCES
    volatile NdsAudioControl *control=(volatile NdsAudioControl *)(uintptr_t)(DOK_AUDIO_ARM7_BOOT+64u);
    control->magic=NDS_AUDIO_CONTROL_MAGIC;
    control->address=(uint32_t)(uintptr_t)&mailbox;
    DC_FlushRange((const void *)control,sizeof(*control));
    control->serial=serial;
    DC_FlushRange((const void *)control,sizeof(*control));
#else
    if (!fifoSendAddress(FIFO_USER_01, &mailbox)) {command_fault=1;return 0;}
#endif
    if(loading)loading(57);
    while (timeout-- && shared->status.command_ack != serial) {
        __asm__ volatile("" ::: "memory");
    }
    /* Keep the fast hardware path, but allow a coarsely scheduled emulator
     * to run ARM7 before declaring a missing acknowledgement. Instruction
     * counts are not a portable deadline across dual-CPU emulators. */
    for (unsigned frame = 0; frame < 8u && shared->status.command_ack != serial; ++frame)
    {
#if !NDS_EMBEDDED_RESOURCES
        (void)fifoCheckAddress(FIFO_USER_01);
#endif
        swiWaitForVBlank();
    }
    int acknowledged=shared->status.command_ack == serial;
    if(!acknowledged)command_fault=1;
    if(loading)loading(58);
    return acknowledged;
}
int nds_audio_init(void *backing, size_t capacity)
{
    NdsAudioCommand message = {0};
    if (!backing || ((uintptr_t)backing & 31u) || capacity < NDS_AUDIO_RING_BYTES)
        return 0;
    if(loading)loading(51);
    memset(&storage, 0, sizeof(storage));
    DC_FlushRange(&storage, sizeof(storage));
    if(loading)loading(52);
    shared = memUncached(&storage);
    if(loading)loading(53);
    ring = backing;
    memset(ring, 0, NDS_AUDIO_RING_BYTES);
    if(loading)loading(54);
    DC_FlushRange(ring, NDS_AUDIO_RING_BYTES);
    if(loading)loading(55);
    prefilled = 0;
    resident_active = 0;
    message.op = NDS_AUDIO_ATTACH;
    message.shared = (uint32_t)(uintptr_t)&storage;
    message.ring = (uint32_t)(uintptr_t)ring;
    if(!command(&message))return 0;
    NdsAudioStatus state=nds_audio_status();
    if(loading)loading(59);
    return state.ready&&!state.stale_commands;
}
void nds_audio_service(void) { /* ARM7 IRQ advances independently of ARM9. */ }
int nds_audio_write(unsigned part, const int8_t *pcm, size_t bytes)
{
    NdsAudioStatus state = nds_audio_status();
    uint32_t ordinal;
    if (!shared || part > 1 || bytes > NDS_AUDIO_PART_BYTES || (bytes && !pcm) ||
        (state.playing && resident_active))
        return 0;
    if (state.playing) {
        if (!state.consumed_parts || part != ((state.consumed_parts - 1u) & 1u))
            return 0;
        ordinal = state.consumed_parts + 1u;
        if (shared->part_ready[part] == ordinal) return 0;
    } else {
        ordinal = part;
        prefilled |= 1u << part;
    }
    if (bytes) memcpy(ring + part * NDS_AUDIO_PART_BYTES, pcm, bytes);
    memset(ring + part * NDS_AUDIO_PART_BYTES + bytes, 0,
           NDS_AUDIO_PART_BYTES - bytes);
    DC_FlushRange(ring + part * NDS_AUDIO_PART_BYTES, NDS_AUDIO_PART_BYTES);
    /* Flush completes before uncached publication. ARM7 never reads a filling
     * part. If its deadline passes, it stops rather than replaying stale PCM. */
    shared->part_ready[part] = ordinal;
    return 1;
}
int nds_audio_begin(uint32_t rate, uint32_t samples, int loop, uint32_t identity)
{
    NdsAudioCommand message = {0};
    NdsAudioStatus state = nds_audio_status();
    if (!shared || !state.ready || state.playing || prefilled != 3u ||
        rate < 8000 || rate > 48000 || !samples || !identity) return 0;
    message.op = NDS_AUDIO_PLAY; message.rate = rate; message.samples = samples;
    message.loop = !!loop; message.identity = identity;
    if (!command(&message)) return 0;
    state = nds_audio_status();
    if (!state.playing || state.identity != identity) return 0;
    prefilled = 0;
    resident_active = 0;
    return 1;
}
int nds_audio_stop(uint32_t identity)
{
    NdsAudioCommand message = {0};
    if (!shared) return 0;
    message.op = NDS_AUDIO_STOP; message.identity = identity;
    return command(&message) && !nds_audio_status().playing;
}
int nds_audio_volume(uint32_t volume)
{
    NdsAudioCommand message = {0};
    if (!shared || volume > 127u) return 0;
    message.op = NDS_AUDIO_VOLUME; message.volume = volume;
    return command(&message);
}
int nds_audio_resident(const unsigned char *packed, uint32_t packed_bytes,
    uint32_t samples, uint32_t rate, int loop, uint32_t identity)
{
    NdsAudioCommand message = {0};
    NdsAudioStatus state = nds_audio_status();
    uintptr_t address = (uintptr_t)packed;
    if (!shared || !state.ready || state.playing || !identity || !samples ||
        samples > 65536u || rate < 8000u || rate > 48000u ||
        packed_bytes < 8u || packed_bytes > 65536u ||
        address < 0x02000000u || address > 0x02400000u - packed_bytes) return 0;
    message.op = NDS_AUDIO_RESIDENT; message.packed = (uint32_t)address;
    message.packed_bytes = packed_bytes; message.samples = samples;
    message.rate = rate; message.loop = !!loop; message.identity = identity;
    if (!command(&message)) return 0;
    state = nds_audio_status();
    if (!state.playing || state.identity != identity) return 0;
    prefilled = 0;
    resident_active = 1;
    return 1;
}
