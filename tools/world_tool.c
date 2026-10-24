#include "dok/world.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void hex(const uint8_t *p,size_t size) {
    size_t i;putchar('"');for(i=0;i<size;++i) printf("%02x",p[i]);putchar('"');
}
int main(int argc,char **argv) {
    FILE *file;long length;uint8_t *archive_bytes=0,*level_maps=0,*world_maps=0;
    dok_level *level=0;dok_level_object *definitions=0;dok_world_object *objects=0;
    dok_res_archive archive;dok_res_entry entry;dok_level_error le;dok_world_error we;dok_world world;
    size_t i;int result=1;
    if(argc!=3) {fprintf(stderr,"Usage: dok-world archive.res LEVEL1\n");return 2;}
    file=fopen(argv[1],"rb");if(!file) {perror(argv[1]);return 1;}
    if(fseek(file,0,SEEK_END) || (length=ftell(file))<0 || fseek(file,0,SEEK_SET)) {fclose(file);return 1;}
    archive_bytes=malloc((size_t)length+1);level_maps=malloc(3*64*64);world_maps=malloc(4*64*64);
    level=calloc(1,sizeof(*level));definitions=calloc(256,sizeof(*definitions));objects=calloc(256,sizeof(*objects));
    if(!archive_bytes || !level_maps || !world_maps || !level || !definitions || !objects) {fclose(file);goto done;}
    if(fread(archive_bytes,1,(size_t)length,file)!=(size_t)length) {fclose(file);goto done;}
    fclose(file);
    if(dok_res_open(&archive,archive_bytes,(size_t)length)!=DOK_RES_OK ||
       dok_res_find(&archive,(const uint8_t *)argv[2],strlen(argv[2]),&entry)!=DOK_RES_OK) {
        fprintf(stderr,"Archive/level lookup failed\n");goto done;}
    if(dok_level_parse(level,archive_bytes+entry.offset,entry.size,definitions,256,level_maps,3*64*64,&le)!=DOK_LEVEL_OK) {
        fprintf(stderr,"LEVEL %u line %lu\n",(unsigned)le.status,(unsigned long)le.line);goto done;}
    if(dok_world_build(&world,level,&archive,objects,256,world_maps,4*64*64,&we)!=DOK_WORLD_OK) {
        fprintf(stderr,"WORLD %u object %u cell %u plane %u glyph %u\n",(unsigned)we.status,
            (unsigned)we.object_index,(unsigned)we.cell,(unsigned)we.plane,(unsigned)we.glyph);goto done;}
    printf("{\"player_index\":%u,\"pickup_99_total\":%d,\"maps\":[",(unsigned)world.player_index,(int)world.pickup_99_total);
    for(i=0;i<4;++i) {if(i) putchar(',');hex(world_maps+i*world.cells,world.cells);}
    printf("],\"objects\":[");
    for(i=0;i<level->object_count;++i) {
        printf("%s{\"glyph\":%u,\"class\":%u,\"size\":%u,\"placements\":%u,\"bytes\":",
            i?",":"",(unsigned)definitions[i].glyph,(unsigned)definitions[i].class_id,
            (unsigned)objects[i].native_size,(unsigned)objects[i].placements);
        hex(objects[i].bytes,sizeof(objects[i].bytes));printf(",\"known\":");hex(objects[i].known,sizeof(objects[i].known));putchar('}');
    }
    printf("]}\n");result=ferror(stdout)?1:0;
done:
    free(archive_bytes);free(level_maps);free(world_maps);free(level);free(definitions);free(objects);return result;
}
