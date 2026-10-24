#ifndef DOK_SOUND_H
#define DOK_SOUND_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum dok_sound_status {
    DOK_SOUND_OK = 0, DOK_SOUND_INVALID_ARGUMENT, DOK_SOUND_TRUNCATED,
    DOK_SOUND_INVALID_SIGNATURE, DOK_SOUND_UNSUPPORTED_FORMAT,
    DOK_SOUND_INVALID_RATE, DOK_SOUND_TRAILING_DATA, DOK_SOUND_INVALID_RANGE,
    DOK_SOUND_BUFFER_TOO_SMALL
} dok_sound_status;
/* Borrowed mono unsigned PCM8. Only the verified SVM disk profile is supported. */
typedef struct dok_sound_view {
    const uint8_t *samples;
    uint32_t sample_count, sample_rate;
    uint16_t parameter_c, parameter_d;
    uint8_t stored_flags;
} dok_sound_view;
/* All outputs remain untouched on failure. Native pointer bytes are ignored. */
dok_sound_status dok_sound_open(const void *data, size_t size, dok_sound_view *out);
dok_sound_status dok_sound_decode_s8(const dok_sound_view *sound, uint32_t first,
                                   size_t count, int8_t *out, size_t capacity);
dok_sound_status dok_sound_decode_s16(const dok_sound_view *sound, uint32_t first,
                                    size_t count, int16_t *out, size_t capacity);
/* Builds the 44-byte RIFF PCM8/mono header. Append samples, then one zero byte if odd.
 * The caller provides at least 44 writable bytes. No IO or allocation in this module.
 */
dok_sound_status dok_sound_wav_header(const dok_sound_view *sound, uint8_t *out, size_t capacity);
const char *dok_sound_status_string(dok_sound_status status);
#ifdef __cplusplus
}
#endif
#endif
