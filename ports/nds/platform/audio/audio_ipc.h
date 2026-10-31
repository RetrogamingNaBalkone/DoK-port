#ifndef DOK_NDS_AUDIO_IPC_H
#define DOK_NDS_AUDIO_IPC_H
#include <stdint.h>
#define NDS_AUDIO_PART_BYTES 32768u
#define NDS_AUDIO_RING_BYTES (2u * NDS_AUDIO_PART_BYTES)
#define NDS_AUDIO_IPC_MAGIC UINT32_C(0x4e313841)
#define NDS_AUDIO_CONTROL_MAGIC UINT32_C(0x43424d41)
/* ARM9-owned cache line following the 64-byte ARM7 boot trace. */
typedef struct { uint32_t magic,address,serial,reserved[5]; } NdsAudioControl;
/* ARM7-owned input cache line after trace64 + ARM9 control32. Touch bytes
 * are the unchanged SDK touchPosition wire representation. */
typedef struct {
    uint32_t sequence,count;
    uint16_t keys,reserved;
    uint16_t touch[6];
    uint32_t padding[2];
} NdsInputMailbox;
enum { NDS_AUDIO_ATTACH = 1, NDS_AUDIO_PLAY, NDS_AUDIO_STOP, NDS_AUDIO_VOLUME,
       NDS_AUDIO_RESIDENT };
/* FIFO sends one mailbox address. ARM9 flushes immutable command bytes and
 * keeps them until ARM7 copies and acknowledges; timeout forbids reuse.
 * In streamed mode ARM9 owns PCM and ready[]; in
 * resident mode ARM7 alone writes PCM, with no ARM9 ring writes. ARM7 owns
 * every status word and channel/timer register. No sound FIFO is installed. */
typedef struct {
    uint32_t magic, op, shared, ring, rate, samples, loop, identity, serial, volume;
    uint32_t packed, packed_bytes;
} NdsAudioCommand;
typedef struct {
    uint32_t ready, playing, identity, consumed_parts, consumed_samples;
    uint32_t underruns, stale_commands, rejected_busy, command_ack, irq_count;
    uint32_t timer_period, status_sequence;
    uint32_t producer_samples, preparing, consumer_started;
} NdsAudioStatus;
typedef struct {
    volatile uint32_t part_ready[2];
    uint32_t padding[6];
    volatile NdsAudioStatus status;
    /* Keep unrelated ARM9 globals off ARM7-owned cache lines. */
    uint32_t status_padding[1];
} NdsAudioShared;
#endif
