#ifndef DOK_NDS_AUDIO_CODEC_H
#define DOK_NDS_AUDIO_CODEC_H
#include "dok/contracts.h"

/* Lossless byte PCM, not ADPCM: modulo256 deltas and per256-sample Rice
 * blocks. Each block stores its byte length for bounded range decoding.
 * No allocation, amplification, resampling or signed arithmetic overflow.
 * Buffers must not overlap. On failure destination may be partially written.
 * The packed bank is runtime data, not a replacement source asset format. */
dok_status dok_audio_pack(const unsigned char *pcm, uint32_t samples,
    unsigned char *packed, uint32_t capacity, uint32_t *packed_bytes);
dok_status dok_audio_decode_range(const unsigned char *packed, uint32_t bytes,
    uint32_t samples, uint32_t offset, unsigned char *pcm, uint32_t count);
#endif
