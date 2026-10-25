#include "dok/rom_assets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void hex(const uint8_t *p,size_t n) {size_t i;putchar('"');for(i=0;i<n;++i) printf("%02x",p[i]);putchar('"');}
static void span_hex(const dok_runtime_level *l,dok_runtime_span s) {hex(l->text+s.offset,s.size);}
int main(int argc,char **argv) {
    FILE *file;long length;uint8_t *bytes=0;dok_runtime *runtime=0;dok_runtime_level rom;
    dok_rom_assets assets;dok_rom_status status;unsigned number,language;size_t i,j;int result=1;
    if(argc!=4) {fprintf(stderr,"Usage: dok-rom-assets assets.bin level(1..3) language(ru|en)\n");return 2;}
    number=(unsigned)strtoul(argv[2],0,10);
    if(number<1 || number>3 || (strcmp(argv[3],"ru") && strcmp(argv[3],"en"))) return 2;
    language=!strcmp(argv[3],"en");
    file=fopen(argv[1],"rb");if(!file) {perror(argv[1]);return 1;}
    if(fseek(file,0,SEEK_END) || (length=ftell(file))<0 || fseek(file,0,SEEK_SET)) {fclose(file);return 1;}
    bytes=malloc((size_t)length+1);runtime=calloc(1,sizeof(*runtime));
    if(!bytes || !runtime) {fclose(file);goto done;}
    if(fread(bytes,1,(size_t)length,file)!=(size_t)length) {fclose(file);goto done;}
    fclose(file);
    status=dok_rom_open(&assets,bytes,(size_t)length);
    if(status!=DOK_ROM_OK) {fprintf(stderr,"ROM validation %u\n",(unsigned)status);goto done;}
    status=dok_rom_level(&rom,&assets,number-1,language);
    if(status!=DOK_ROM_OK) {fprintf(stderr,"ROM level %u\n",(unsigned)status);goto done;}
    if(dok_runtime_reset(runtime,&rom)!=DOK_RUNTIME_OK) {fprintf(stderr,"Runtime reset failed\n");goto done;}
    printf("{\"settings\":[");for(i=0;i<DOK_LEVEL_SETTING_COUNT;++i) printf("%s%u",i?",":"",rom.settings[i]);
    printf("],\"glyph_index\":[");for(i=0;i<256;++i) printf("%s%u",i?",":"",rom.glyph_index[i]);
    printf("],\"palette\":");span_hex(&rom,rom.palette);printf(",\"light\":");span_hex(&rom,rom.light);
    printf(",\"player_index\":%u,\"pickup_99_total\":%d,\"maps\":[",runtime->player_index,runtime->pickup_99_total);
    for(i=0;i<4;++i) {if(i) putchar(',');hex(runtime->maps+i*rom.cells,rom.cells);}
    printf("],\"objects\":[");
    for(i=0;i<rom.object_count;++i) {
        const dok_runtime_definition *d=&rom.definitions[i];const dok_runtime_actor *a=dok_runtime_actor_at(runtime,(uint16_t)i);
        uint8_t bytes[DOK_WORLD_STATE_BYTES]={0},known[DOK_WORLD_STATE_BYTES/8]={0};uint32_t n;
        printf("%s{\"glyph\":%u,\"class\":%u,\"flags\":%u,\"sprite_flags\":%u,\"width_parameter\":%d,\"height_parameter\":%d,\"marker\":%u,\"placements\":%u,\"actor_index\":%u,\"sprite\":",
            i?",":"",d->glyph,d->class_id,d->flags,d->sprite_flags,d->width_parameter,d->height_parameter,d->marker,d->placements,d->actor_index);
        span_hex(&rom,d->sprite);printf(",\"animation\":");span_hex(&rom,d->animation);printf(",\"sound\":");span_hex(&rom,d->sound);
        printf(",\"fields\":[");
        for(j=0;j<d->field_count;++j) {
            const dok_runtime_field *f=&rom.fields[d->field_begin+j];
            printf("%s{\"offset\":%u,\"kind\":%u,",j?",":"",f->native_offset,f->kind);
            if(f->kind<=DOK_LEVEL_CHAR) printf("\"number\":%ld",(long)f->value.number);
            else {printf("\"hex\":");span_hex(&rom,f->value.bytes);}
            putchar('}');
        }
        for(j=0;j<DOK_WORLD_STATE_BYTES;++j) if(dok_runtime_read_u(runtime,(uint16_t)i,j,1,&n)) {
            bytes[j]=(uint8_t)n;known[j/8]|=(uint8_t)(1u<<(j%8));
        }
        printf("],\"bytes\":");hex(bytes,sizeof(bytes));printf(",\"known\":");hex(known,sizeof(known));printf(",\"actor\":");
        if(a) printf("{\"known\":%lu,\"x\":%ld,\"y\":%ld,\"z\":%ld,\"base_speed\":%ld,\"heading\":%d,\"bound_9e\":%d,\"bound_a0\":%d}",
            (unsigned long)a->known,(long)a->x,(long)a->y,(long)a->z,(long)a->base_speed,a->heading,a->bound_9e,a->bound_a0);
        else printf("null");
        putchar('}');
    }
    printf("],\"memory\":{\"object_count\":%u,\"actor_count\":%u,\"field_count\":%u,\"text_size\":%u,\"cells\":%u,\"mutable_used\":%lu,\"rom_records_used\":%lu}}\n",
        rom.object_count,rom.actor_count,rom.field_count,rom.text_size,rom.cells,
        (unsigned long)(rom.object_count*sizeof(dok_runtime_object)+rom.actor_count*sizeof(dok_runtime_actor)+sizeof(dok_runtime_player)+4u*rom.cells),
        (unsigned long)(rom.object_count*(sizeof(dok_runtime_definition)+sizeof(dok_runtime_object))+rom.actor_count*sizeof(dok_runtime_actor)+sizeof(dok_runtime_player)+rom.field_count*sizeof(dok_runtime_field)+rom.text_size+3u*rom.cells));
    result=ferror(stdout)?1:0;
done:
    free(bytes);free(runtime);return result;
}
