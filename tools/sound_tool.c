#include "dok/sound.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t hash_byte(uint32_t h,uint8_t b) { return (h^b)*UINT32_C(16777619); }
static FILE *output_file(const char *dir,const char *name) {
    char path[1024];
    int n=snprintf(path,sizeof(path),"%s/%s",dir,name);
    if(n<0 || (size_t)n>=sizeof(path)) return NULL;
    return fopen(path,"wb");
}
static int export_wav(const dok_sound_view *s,const char *dir) {
    uint8_t header[44],pad=0;
    FILE *f;
    int failed;
    if(dok_sound_wav_header(s,header,sizeof(header))!=DOK_SOUND_OK) return 1;
    f=output_file(dir,"pcm8.wav");
    if(!f) return 1;
    failed=fwrite(header,1,sizeof(header),f)!=sizeof(header);
    if(!failed && fwrite(s->samples,1,s->sample_count,f)!=s->sample_count) failed=1;
    if(!failed && (s->sample_count&1u) && fwrite(&pad,1,1,f)!=1) failed=1;
    if(fclose(f)) failed=1;
    return failed;
}
int main(int argc,char **argv) {
    FILE *file,*s8file=NULL,*s16file=NULL;
    uint8_t *bytes;
    long length;
    int exporting,failed=0;
    dok_sound_view sound;
    dok_sound_status status;
    uint32_t first=0,h8=UINT32_C(2166136261),hs8=UINT32_C(2166136261),hs16=UINT32_C(2166136261);
    if(!((argc==3 && !strcmp(argv[1],"--list")) || (argc==4 && !strcmp(argv[1],"--export")))) {
        fprintf(stderr,"Usage: dok-sound --list FILE | --export FILE EXISTING_DIRECTORY\n"); return 2;
    }
    exporting=argc==4;
    file=fopen(argv[2],"rb");
    if(!file) { perror(argv[2]); return 2; }
    if(fseek(file,0,SEEK_END) || (length=ftell(file))<0 || fseek(file,0,SEEK_SET)) { fclose(file); return 2; }
    bytes=(uint8_t *)malloc(length?(size_t)length:1u);
    if(!bytes) { fclose(file); return 2; }
    if(fread(bytes,1,(size_t)length,file)!=(size_t)length || ferror(file)) { fclose(file); free(bytes); return 2; }
    fclose(file);
    status=dok_sound_open(bytes,(size_t)length,&sound);
    if(status!=DOK_SOUND_OK) { fprintf(stderr,"%s\n",dok_sound_status_string(status)); free(bytes); return 1; }
    if(exporting) {
        if(export_wav(&sound,argv[3])) { free(bytes); return 2; }
        s8file=output_file(argv[3],"pcm_s8.raw"); s16file=output_file(argv[3],"pcm_s16le.raw");
        if(!s8file || !s16file) {
            if(s8file) fclose(s8file);
            if(s16file) fclose(s16file);
            free(bytes); return 2;
        }
    }
    while(first<sound.sample_count) {
        int8_t signed8[1024]; int16_t signed16[1024]; uint8_t raw8[1024],raw16[2048];
        size_t count=(size_t)(sound.sample_count-first),i;
        if(count>1024u) count=1024u;
        if(dok_sound_decode_s8(&sound,first,count,signed8,1024)!=DOK_SOUND_OK ||
           dok_sound_decode_s16(&sound,first,count,signed16,1024)!=DOK_SOUND_OK) { failed=1; break; }
        for(i=0;i<count;++i) {
            uint16_t word=(uint16_t)signed16[i];
            raw8[i]=(uint8_t)signed8[i]; raw16[i*2]=(uint8_t)word; raw16[i*2+1]=(uint8_t)(word>>8);
            h8=hash_byte(h8,sound.samples[(size_t)first+i]); hs8=hash_byte(hs8,raw8[i]);
            hs16=hash_byte(hash_byte(hs16,raw16[i*2]),raw16[i*2+1]);
        }
        if(exporting && (fwrite(raw8,1,count,s8file)!=count || fwrite(raw16,2,count,s16file)!=count)) { failed=1; break; }
        first+=(uint32_t)count;
    }
    if(s8file && fclose(s8file)) failed=1;
    if(s16file && fclose(s16file)) failed=1;
    if(!failed) {
        puts("sample_rate\tsample_count\tparameter_c\tparameter_d\tfnv_u8\tfnv_s8\tfnv_s16le");
        printf("%lu\t%lu\t%u\t%u\t%08lx\t%08lx\t%08lx\n",(unsigned long)sound.sample_rate,
               (unsigned long)sound.sample_count,(unsigned)sound.parameter_c,(unsigned)sound.parameter_d,
               (unsigned long)h8,(unsigned long)hs8,(unsigned long)hs16);
    }
    free(bytes);
    return failed?2:0;
}
