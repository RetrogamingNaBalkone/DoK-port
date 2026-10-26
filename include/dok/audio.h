#ifndef DOK_AUDIO_H
#define DOK_AUDIO_H
#include "dok/sound.h"
/* One borrowed PCM voice. A busy voice rejects starts, including itself;
 * same-object requests change requested_loop before the busy check;
 * the backend's latched loop mode stays unchanged until a successful start.
 * Stop is identity-sensitive. Pulling the last byte ends a one-shot. */
typedef struct {dok_sound_view sound;uint32_t position,loops,starts,rejected,stops,resource;uint8_t active,loop,requested_loop;} dok_audio;
void dok_audio_init(dok_audio *);
int dok_audio_play(dok_audio *,const dok_sound_view *,uint32_t,int);
int dok_audio_stop(dok_audio *,uint32_t);
int dok_audio_playing(const dok_audio *,uint32_t);
size_t dok_audio_pull(dok_audio *,int8_t *,size_t);
#endif
