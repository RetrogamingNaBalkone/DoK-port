/* Evidence: RUS 1108:00ec, 10e8:007c, 1118:046e/0829. docs/originals/sound.md. */
#include "dok/sound.h"
static uint16_t u16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void put32(uint8_t *p, uint32_t value) {
    p[0]=(uint8_t)value; p[1]=(uint8_t)(value>>8); p[2]=(uint8_t)(value>>16); p[3]=(uint8_t)(value>>24);
}
dok_sound_status dok_sound_open(const void *data, size_t size, dok_sound_view *out) {
    const uint8_t *p=(const uint8_t *)data;
    dok_sound_view sound;
    if (!data || !out) return DOK_SOUND_INVALID_ARGUMENT;
    if (size<36u) return DOK_SOUND_TRUNCATED;
    /* +0 is a serialized native vtable value, not a format magic. Binding skips it. */
    if (p[2]!='S' || p[3]!='V' || p[4]!='M') return DOK_SOUND_INVALID_SIGNATURE;
    if (u16(p+5)!=1u || p[7]!=9u || u16(p+8)!=8u || u16(p+10)!=0u) return DOK_SOUND_UNSUPPORTED_FORMAT;
    sound.sample_rate=u32(p+16); sound.sample_count=u32(p+20);
    if (!sound.sample_rate || sound.sample_rate>65535u) return DOK_SOUND_INVALID_RATE;
    if ((size_t)sound.sample_count>size-36u) return DOK_SOUND_TRUNCATED;
    if ((size_t)sound.sample_count!=size-36u) return DOK_SOUND_TRAILING_DATA;
    sound.samples=p+36; sound.parameter_c=u16(p+12); sound.parameter_d=u16(p+14); sound.stored_flags=p[7];
    *out=sound;
    return DOK_SOUND_OK;
}
static dok_sound_status range(const dok_sound_view *sound, uint32_t first, size_t count,
                              const void *out, size_t capacity) {
    if (!sound || !sound->samples || (count && !out)) return DOK_SOUND_INVALID_ARGUMENT;
    if (first>sound->sample_count || count>(size_t)(sound->sample_count-first)) return DOK_SOUND_INVALID_RANGE;
    if (count>capacity) return DOK_SOUND_BUFFER_TOO_SMALL;
    return DOK_SOUND_OK;
}
dok_sound_status dok_sound_decode_s8(const dok_sound_view *sound, uint32_t first,
                                   size_t count, int8_t *out, size_t capacity) {
    size_t i;
    dok_sound_status status=range(sound,first,count,out,capacity);
    if (status!=DOK_SOUND_OK) return status;
    for (i=0;i<count;++i) out[i]=(int8_t)((int16_t)sound->samples[(size_t)first+i]-128);
    return DOK_SOUND_OK;
}
dok_sound_status dok_sound_decode_s16(const dok_sound_view *sound, uint32_t first,
                                    size_t count, int16_t *out, size_t capacity) {
    size_t i;
    dok_sound_status status=range(sound,first,count,out,capacity);
    if (status!=DOK_SOUND_OK) return status;
    for (i=0;i<count;++i) out[i]=(int16_t)(((int32_t)sound->samples[(size_t)first+i]-128)*256);
    return DOK_SOUND_OK;
}
dok_sound_status dok_sound_wav_header(const dok_sound_view *sound, uint8_t *out, size_t capacity) {
    uint8_t header[44]={
        'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,
        1,0,1,0,0,0,0,0,0,0,0,0,1,0,8,0,'d','a','t','a',0,0,0,0
    };
    size_t i;
    uint32_t padding;
    if (!sound || !out || !sound->samples) return DOK_SOUND_INVALID_ARGUMENT;
    if (!sound->sample_rate || sound->sample_rate>65535u) return DOK_SOUND_INVALID_RATE;
    if (capacity<44u) return DOK_SOUND_BUFFER_TOO_SMALL;
    padding=sound->sample_count&1u;
    if (sound->sample_count>UINT32_MAX-36u-padding) return DOK_SOUND_INVALID_RANGE;
    put32(header+4,36u+sound->sample_count+padding);
    put32(header+24,sound->sample_rate); put32(header+28,sound->sample_rate);
    put32(header+40,sound->sample_count);
    for (i=0;i<44u;++i) out[i]=header[i];
    return DOK_SOUND_OK;
}
const char *dok_sound_status_string(dok_sound_status s) {
    switch (s) {
    case DOK_SOUND_OK: return "ok";
    case DOK_SOUND_INVALID_ARGUMENT: return "invalid_argument";
    case DOK_SOUND_TRUNCATED: return "truncated";
    case DOK_SOUND_INVALID_SIGNATURE: return "invalid_signature";
    case DOK_SOUND_UNSUPPORTED_FORMAT: return "unsupported_format";
    case DOK_SOUND_INVALID_RATE: return "invalid_rate";
    case DOK_SOUND_TRAILING_DATA: return "trailing_data";
    case DOK_SOUND_INVALID_RANGE: return "invalid_range";
    case DOK_SOUND_BUFFER_TOO_SMALL: return "buffer_too_small";
    default: return "unknown_status";
    }
}
