#include "dok/audio.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#undef assert
#define assert(x) do{if(!(x)){fprintf(stderr,"G16 failed line %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(void){
 const uint8_t samples[]={0,128,255,17,229};int8_t out[32];dok_sound_view s={samples,5,22050,255,128,9};dok_audio a;
 dok_audio_init(&a);assert(!dok_audio_playing(&a,1));assert(!dok_audio_play(&a,&s,0,0));
 assert(dok_audio_play(&a,&s,1,0));assert(dok_audio_pull(&a,out,3)==3);assert(out[0]==-128 && out[1]==0 && out[2]==127);
 assert(!dok_audio_play(&a,&s,2,1));assert(!dok_audio_stop(&a,2));assert(dok_audio_playing(&a,1));
 assert(!dok_audio_play(&a,&s,1,1));assert(a.requested_loop && !a.loop && a.position==3);
 assert(dok_audio_pull(&a,out,8)==2 && !a.active);assert(out[0]==-111 && out[1]==101);for(unsigned i=2;i<8;i++)assert(!out[i]);
 assert(dok_audio_play(&a,&s,1,1));assert(dok_audio_pull(&a,out,32)==32);for(unsigned i=0;i<32;i++)assert(out[i]==(int8_t)(samples[i%5]-128));assert(a.loops==6 && a.position==2);
 assert(!dok_audio_play(&a,&s,1,0));assert(!a.requested_loop && a.loop);assert(dok_audio_pull(&a,out,32)==32 && a.active);
 assert(dok_audio_stop(&a,1));assert(!dok_audio_stop(&a,1));assert(dok_audio_pull(&a,out,32)==0);
 assert(dok_audio_play(&a,&s,2,0));assert(a.position==0);assert(dok_audio_pull(&a,out,32)==5 && !a.active);
 assert(dok_audio_play(&a,&s,0x1234fff2u,1));assert(!dok_audio_playing(&a,0x5678fff2u));assert(!dok_audio_stop(&a,0x5678fff2u));
 assert(!dok_audio_play(&a,&s,0x5678fff2u,0));assert(a.loop && a.active);assert(dok_audio_stop(&a,0x1234fff2u));
 printf("G16 host: mono reject, latched loop, identity stop, odd loop continuity, EOF silence, restart passed; dok_audio=%zu\n",sizeof(a));return 0;
}
