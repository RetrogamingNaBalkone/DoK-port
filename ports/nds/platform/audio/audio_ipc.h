#ifndef DOK_NDS_AUDIO_IPC_H
#define DOK_NDS_AUDIO_IPC_H
#include <stdint.h>
#define NDS_AUDIO_PART_BYTES 32768u
#define NDS_AUDIO_RING_BYTES (2u * NDS_AUDIO_PART_BYTES)
#define NDS_AUDIO_IPC_MAGIC UINT32_C(0x4e313841)
enum { NDS_AUDIO_ATTACH = 1, NDS_AUDIO_PLAY, NDS_AUDIO_STOP, NDS_AUDIO_VOLUME,
       NDS_AUDIO_RESIDENT };
/* FIFO copies commands. In streamed mode ARM9 owns PCM and ready[]; in
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
