#ifndef DOK_NDS_AUDIO_ARM9_H
#define DOK_NDS_AUDIO_ARM9_H
#include <stddef.h>
#include <stdint.h>
#include "audio_ipc.h"
/* backing must be 32-byte aligned main RAM, >=65536 bytes, persistent until
 * shutdown. Only the first 65536 bytes are used. No allocations or file I/O. */
int nds_audio_init(void *backing, size_t capacity);
/* Before begin, write both parts. While active, only the retired part may be
 * written, once per retirement. Short parts are zero padded, never repeated.
 * Caller fills looping source across part boundaries without padding. */
int nds_audio_write(unsigned part, const int8_t *pcm, size_t bytes);
int nds_audio_begin(uint32_t rate, uint32_t total_samples, int loop,
                    uint32_t identity);
/* 0 is the global lifecycle stop; other identities stop only their own voice. */
int nds_audio_stop(uint32_t identity);
/* Global linear hardware volume, 0..127. Applies immediately and to next play. */
int nds_audio_volume(uint32_t volume);
/* Caller flushes the immutable packed bank once and keeps it resident until
 * stop. ARM7 owns ring production; ACK admits a preparing voice immediately. */
int nds_audio_resident(const unsigned char *packed, uint32_t packed_bytes,
    uint32_t samples, uint32_t rate, int loop, uint32_t identity);
void nds_audio_service(void);
NdsAudioStatus nds_audio_status(void);
#endif
