#include <nds.h>
#include <string.h>
#include "arm9.h"
static NdsAudioShared storage __attribute__((aligned(32)));
static volatile NdsAudioShared *shared;
static int8_t *ring;
static uint32_t serial;
static unsigned prefilled;
static int resident_active;

NdsAudioStatus nds_audio_status(void)
{
    NdsAudioStatus result = {0};
    if (shared) {
        uint32_t before, after;
        do {
            before = shared->status.status_sequence;
            result = shared->status;
            after = shared->status.status_sequence;
        } while ((before & 1u) || before != after);
    }
    return result;
}
static int command(NdsAudioCommand *message)
{
    unsigned timeout = 200000;
    message->magic = NDS_AUDIO_IPC_MAGIC;
    message->serial = ++serial;
    if (!fifoSendDatamsg(FIFO_USER_01, sizeof(*message), (u8 *)message)) return 0;
    while (timeout-- && shared->status.command_ack != serial) {
        __asm__ volatile("" ::: "memory");
    }
    return shared->status.command_ack == serial;
}
int nds_audio_init(void *backing, size_t capacity)
{
    NdsAudioCommand message = {0};
    if (!backing || ((uintptr_t)backing & 31u) || capacity < NDS_AUDIO_RING_BYTES)
        return 0;
    memset(&storage, 0, sizeof(storage));
    DC_FlushRange(&storage, sizeof(storage));
    shared = memUncached(&storage);
    ring = backing;
    memset(ring, 0, NDS_AUDIO_RING_BYTES);
    DC_FlushRange(ring, NDS_AUDIO_RING_BYTES);
    prefilled = 0;
    resident_active = 0;
    message.op = NDS_AUDIO_ATTACH;
    message.shared = (uint32_t)(uintptr_t)&storage;
    message.ring = (uint32_t)(uintptr_t)ring;
    return command(&message) && shared->status.ready;
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
