#include "dok/runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void hex(const uint8_t *p,size_t n) {size_t i;putchar('"');for(i=0;i<n;++i) printf("%02x",p[i]);putchar('"');}
static void span_hex(const dok_runtime_level *l,dok_runtime_span s) {hex(l->text+s.offset,s.size);}
static void layout(void) {
#define T(name,type) printf("\"%s\":{\"size\":%lu,\"align\":%lu}",name,(unsigned long)sizeof(type),(unsigned long)_Alignof(type))
    putchar('{');T("runtime",dok_runtime);putchar(',');T("object",dok_runtime_object);putchar(',');T("actor",dok_runtime_actor);
    putchar(',');T("player",dok_runtime_player);putchar(',');T("definition",dok_runtime_definition);putchar(',');T("field",dok_runtime_field);
    putchar(',');T("level",dok_runtime_level);putchar(',');T("storage",dok_runtime_storage);
    printf(",\"offsets\":{\"runtime.objects\":%lu,\"runtime.actors\":%lu,\"runtime.player\":%lu,\"runtime.maps\":%lu,\"actor.x\":%lu,\"actor.heading\":%lu,\"object.state\":%lu,\"definition.actor_index\":%lu,\"field.value\":%lu}",
        (unsigned long)offsetof(dok_runtime,objects),(unsigned long)offsetof(dok_runtime,actors),(unsigned long)offsetof(dok_runtime,player),
        (unsigned long)offsetof(dok_runtime,maps),(unsigned long)offsetof(dok_runtime_actor,x),(unsigned long)offsetof(dok_runtime_actor,heading),
        (unsigned long)offsetof(dok_runtime_object,state),(unsigned long)offsetof(dok_runtime_definition,actor_index),(unsigned long)offsetof(dok_runtime_field,value));
    printf(",\"capacities\":{\"objects\":%u,\"actors\":%u,\"fields\":%u,\"text\":%u,\"cells\":%u}}\n",
        DOK_RUNTIME_MAX_OBJECTS,DOK_RUNTIME_MAX_ACTORS,DOK_RUNTIME_MAX_FIELDS,DOK_RUNTIME_MAX_TEXT,DOK_RUNTIME_MAX_CELLS);
#undef T
}
int main(int argc,char **argv) {
    FILE *file;long length;uint8_t *archive_bytes=0,*level_maps=0,*world_maps=0;
    dok_level *level=0;dok_level_object *definitions=0;dok_world_object *objects=0;
    dok_runtime_storage *storage=0;dok_runtime *runtime=0;dok_runtime_level rom;
    dok_res_archive archive;dok_res_entry entry;dok_level_error le;dok_world_error we;dok_world world;dok_runtime_error re;
    size_t i,j;int result=1;
    if(argc==2 && !strcmp(argv[1],"--layout")) {layout();return ferror(stdout)?1:0;}
    if(argc!=3) {fprintf(stderr,"Usage: dok-runtime archive.res LEVEL1 | --layout\n");return 2;}
    file=fopen(argv[1],"rb");if(!file) {perror(argv[1]);return 1;}
    if(fseek(file,0,SEEK_END) || (length=ftell(file))<0 || fseek(file,0,SEEK_SET)) {fclose(file);return 1;}
    archive_bytes=malloc((size_t)length+1);level_maps=malloc(3*64*64);world_maps=malloc(4*64*64);
    level=calloc(1,sizeof(*level));definitions=calloc(256,sizeof(*definitions));objects=calloc(256,sizeof(*objects));
    storage=calloc(1,sizeof(*storage));runtime=calloc(1,sizeof(*runtime));
    if(!archive_bytes || !level_maps || !world_maps || !level || !definitions || !objects || !storage || !runtime) {fclose(file);goto done;}
    if(fread(archive_bytes,1,(size_t)length,file)!=(size_t)length) {fclose(file);goto done;}
    fclose(file);
    if(dok_res_open(&archive,archive_bytes,(size_t)length)!=DOK_RES_OK ||
       dok_res_find(&archive,(const uint8_t *)argv[2],strlen(argv[2]),&entry)!=DOK_RES_OK) {
        fprintf(stderr,"Archive/level lookup failed\n");goto done;}
    if(dok_level_parse(level,archive_bytes+entry.offset,entry.size,definitions,256,level_maps,3*64*64,&le)!=DOK_LEVEL_OK) {
        fprintf(stderr,"LEVEL %u line %lu\n",(unsigned)le.status,(unsigned long)le.line);goto done;}
    if(dok_world_build(&world,level,&archive,objects,256,world_maps,4*64*64,&we)!=DOK_WORLD_OK) {
        fprintf(stderr,"WORLD %u object %u\n",(unsigned)we.status,(unsigned)we.object_index);goto done;}
    if(dok_runtime_build(&rom,storage,&world,&re)!=DOK_RUNTIME_OK) {
        fprintf(stderr,"RUNTIME %u object %u offset %u\n",(unsigned)re.status,(unsigned)re.object_index,(unsigned)re.native_offset);goto done;}
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
    free(archive_bytes);free(level_maps);free(world_maps);free(level);free(definitions);free(objects);free(storage);free(runtime);return result;
}
