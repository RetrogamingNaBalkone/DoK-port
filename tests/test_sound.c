#include "dok/sound.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 1; } } while (0)
int main(void) {
    uint8_t bytes[41]={0xfd,4,'S','V','M',1,0,9,8,0,0,0,255,0,128,0,0x22,0x56,0,0,4};
    uint8_t bad[41], header[44];
    dok_sound_view s, saved;
    int8_t s8[4]={0}; int16_t s16[4]={0}; size_t i;
    memset(bytes+24,0xff,12); /* Never resolve any serialized native pointer. */
    bytes[36]=0; bytes[37]=127; bytes[38]=128; bytes[39]=255;
    CHECK(dok_sound_open(bytes,40,&s)==DOK_SOUND_OK);
    CHECK(s.sample_count==4 && s.sample_rate==22050 && s.samples==bytes+36);
    CHECK(dok_sound_decode_s8(&s,0,4,s8,4)==DOK_SOUND_OK);
    CHECK(s8[0]==-128 && s8[1]==-1 && s8[2]==0 && s8[3]==127);
    CHECK(dok_sound_decode_s16(&s,0,4,s16,4)==DOK_SOUND_OK);
    CHECK(s16[0]==-32768 && s16[1]==-256 && s16[2]==0 && s16[3]==32512);
    CHECK(dok_sound_decode_s16(&s,2,2,s16,4)==DOK_SOUND_OK && s16[0]==0 && s16[1]==32512);
    s16[0]=123;
    CHECK(dok_sound_decode_s16(&s,0,2,s16,1)==DOK_SOUND_BUFFER_TOO_SMALL && s16[0]==123);
    CHECK(dok_sound_decode_s16(&s,3,SIZE_MAX,s16,4)==DOK_SOUND_INVALID_RANGE && s16[0]==123);
    CHECK(dok_sound_decode_s8(&s,5,0,NULL,0)==DOK_SOUND_INVALID_RANGE);
    CHECK(dok_sound_decode_s8(&s,4,0,NULL,0)==DOK_SOUND_OK);
    CHECK(dok_sound_wav_header(&s,header,44)==DOK_SOUND_OK);
    CHECK(!memcmp(header,"RIFF",4) && !memcmp(header+8,"WAVEfmt ",8));
    CHECK(header[4]==40 && header[24]==0x22 && header[25]==0x56 && header[40]==4 && header[34]==8);
    memcpy(&saved,&s,sizeof(s));
    for(i=0;i<40;++i) { CHECK(dok_sound_open(bytes,i,&s)!=DOK_SOUND_OK); CHECK(!memcmp(&saved,&s,sizeof(s))); }
    CHECK(dok_sound_open(bytes,41,&s)==DOK_SOUND_TRAILING_DATA);
    memcpy(bad,bytes,sizeof(bad)); bad[0]=0xf2;
    CHECK(dok_sound_open(bad,40,&s)==DOK_SOUND_OK); /* Native vtable bytes do not identify PCM. */
    bad[2]='X';
    CHECK(dok_sound_open(bad,40,&s)==DOK_SOUND_INVALID_SIGNATURE);
    memcpy(bad,bytes,sizeof(bad)); bad[8]=16;
    CHECK(dok_sound_open(bad,40,&s)==DOK_SOUND_UNSUPPORTED_FORMAT);
    memcpy(bad,bytes,sizeof(bad)); bad[7]=0x49;
    CHECK(dok_sound_open(bad,40,&s)==DOK_SOUND_UNSUPPORTED_FORMAT);
    memcpy(bad,bytes,sizeof(bad)); memset(bad+16,0,4);
    CHECK(dok_sound_open(bad,40,&s)==DOK_SOUND_INVALID_RATE);
    memcpy(bad,bytes,sizeof(bad)); memset(bad+20,0xff,4);
    CHECK(dok_sound_open(bad,40,&s)==DOK_SOUND_TRUNCATED);
    memcpy(bad,bytes,sizeof(bad)); bad[20]=3;
    CHECK(dok_sound_open(bad,39,&s)==DOK_SOUND_OK);
    CHECK(dok_sound_wav_header(&s,header,44)==DOK_SOUND_OK && header[4]==40 && header[40]==3);
    CHECK(dok_sound_wav_header(&s,header,43)==DOK_SOUND_BUFFER_TOO_SMALL);
    bad[20]=0;
    CHECK(dok_sound_open(bad,36,&s)==DOK_SOUND_OK && s.sample_count==0);
    CHECK(dok_sound_decode_s16(&s,0,0,NULL,0)==DOK_SOUND_OK);
    CHECK(dok_sound_wav_header(&s,header,44)==DOK_SOUND_OK && header[4]==36 && header[40]==0);
    s.sample_count=UINT32_MAX;
    CHECK(dok_sound_wav_header(&s,header,44)==DOK_SOUND_INVALID_RANGE);
    CHECK(dok_sound_open(NULL,0,&s)==DOK_SOUND_INVALID_ARGUMENT);
    puts("sound: signed PCM endpoints, streaming ranges, truncated data, format rejection and RIFF padding passed");
    return 0;
}
