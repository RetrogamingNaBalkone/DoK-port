#include "dok/audio.h"
void dok_audio_init(dok_audio *a){if(a)*a=(dok_audio){0};}
int dok_audio_play(dok_audio *a,const dok_sound_view *s,uint32_t id,int loop){
 if(!a || !s || !id || !s->samples || !s->sample_count || !s->sample_rate)return 0;
 if(a->active){if(a->resource==id)a->requested_loop=(uint8_t)!!loop;++a->rejected;return 0;}
 a->sound=*s;a->resource=id;a->position=0;a->loop=(uint8_t)!!loop;a->requested_loop=a->loop;a->active=1;++a->starts;return 1;
}
int dok_audio_stop(dok_audio *a,uint32_t id){if(!a || !a->active || a->resource!=id)return 0;a->active=0;++a->stops;return 1;}
int dok_audio_playing(const dok_audio *a,uint32_t id){return a && a->active && a->resource==id;}
size_t dok_audio_pull(dok_audio *a,int8_t *out,size_t n){
 size_t used=0;if(!a || !out)return 0;
 while(used<n && a->active){out[used++]=(int8_t)(a->sound.samples[a->position++]-128);
  if(a->position==a->sound.sample_count){if(a->loop){a->position=0;++a->loops;}else a->active=0;}}
 for(size_t i=used;i<n;i++)out[i]=0;return used;
}
