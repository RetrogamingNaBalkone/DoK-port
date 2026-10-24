#include "dok/level.h"
#include <stdio.h>
#include <stdlib.h>

static void bytes(const dok_level *level,dok_level_span span) {
    uint32_t i; putchar('"');
    for(i=0;i<span.size;++i) printf("%02x",level->source[span.offset+i]);
    putchar('"');
}
int main(int argc,char **argv) {
    FILE *file; long length; uint8_t *source,*maps;
    dok_level *level; dok_level_object *objects; dok_level_error error;
    size_t i,j,cells; dok_level_status status;
    if(argc!=2) {fprintf(stderr,"Usage: dok-level LEVEL.bin (JSON, CP866 strings as hex)\n");return 2;}
    file=fopen(argv[1],"rb");if(!file) {perror(argv[1]);return 1;}
    if(fseek(file,0,SEEK_END) || (length=ftell(file))<0 || fseek(file,0,SEEK_SET)) {fclose(file);return 1;}
    source=malloc((size_t)length+1);maps=malloc(3*64*64);
    level=calloc(1,sizeof(*level));objects=calloc(256,sizeof(*objects));
    if(!source || !maps || !level || !objects) {fclose(file);free(source);free(maps);free(level);free(objects);return 1;}
    if(fread(source,1,(size_t)length,file)!=(size_t)length) {fclose(file);free(source);free(maps);free(level);free(objects);return 1;}
    fclose(file);
    status=dok_level_parse(level,source,(size_t)length,objects,256,maps,3*64*64,&error);
    if(status!=DOK_LEVEL_OK) {
        fprintf(stderr,"LEVEL error %u at line %lu\n",(unsigned)status,(unsigned long)error.line);
        free(source);free(maps);free(level);free(objects);return 1;
    }
    printf("{\"settings\":[");
    for(i=0;i<8;++i) printf("%s%u",i?",":"",(unsigned)level->settings[i]);
    printf("],\"palette\":");bytes(level,level->palette);
    printf(",\"light\":");bytes(level,level->light);
    printf(",\"ignored_step_ver\":%lu,\"truncated_messages\":%lu,\"objects\":[",
        (unsigned long)level->ignored_step_ver,(unsigned long)level->truncated_messages);
    for(i=0;i<level->object_count;++i) {
        const dok_level_object *o=&objects[i];
        printf("%s{\"glyph\":%u,\"class\":%u,\"flags\":%u,\"width\":%d,\"height\":%d,\"marker\":%u,\"sprite_flags\":%u,\"sprite\":",
            i?",":"",(unsigned)o->glyph,(unsigned)o->class_id,(unsigned)o->flags,
            (int)o->width_parameter,(int)o->height_parameter,(unsigned)o->marker,(unsigned)o->sprite_flags);
        bytes(level,o->sprite);printf(",\"animation\":");bytes(level,o->animation);
        printf(",\"sound\":");bytes(level,o->sound);printf(",\"fields\":[");
        for(j=0;j<o->field_count;++j) {
            const dok_level_field *f=&o->fields[j];
            printf("%s[%u,%u,",j?",":"",(unsigned)f->kind,(unsigned)f->native_offset);
            if(f->kind<=DOK_LEVEL_CHAR) printf("%ld",(long)f->value.number);
            else bytes(level,f->value.bytes);
            putchar(']');
        }
        printf("]}");
    }
    printf("],\"maps\":[");cells=(size_t)level->settings[0]*level->settings[2];
    for(i=0;i<3;++i) {
        printf("%s\"",i?",":"");
        for(j=0;j<cells;++j) printf("%02x",maps[i*cells+j]);
        putchar('"');
    }
    printf("]}\n");free(source);free(maps);free(level);free(objects);return 0;
}
