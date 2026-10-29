#include "dok/geometry.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
static dok_level_definition level,candidate_level;
static dok_initial_world world,candidate_world;
static dok_geometry_binding bindings[256];
static dok_geometry_cell cell,candidate_cell;
static unsigned char source[65536],binding_bytes[8192];
static uint16_t word(const unsigned char *p) {return (uint16_t)((uint16_t)p[0]|(uint16_t)((uint16_t)p[1]<<8));}
int main(int argc,char **argv)
{
    dok_asset_view view; FILE *file; size_t bytes; uint32_t i,j,x,z,k;
    if(argc!=3) return 1;
    file=fopen(argv[1],"rb"); if(file==NULL) return 2;
    bytes=fread(source,1u,sizeof(source),file); fclose(file);
    memset(&view,0,sizeof(view)); view.entry.language=1u;
    view.entry.representation=DOK_ASSET_LEVEL_CP866;
    view.entry.bytes=(uint32_t)bytes; view.data=source;
    if(dok_level_parse(&view,&candidate_level,&level)!=DOK_WORLD_OK) return 3;
    if(dok_initial_world_build(&level,1u,&candidate_world,&world)!=DOK_WORLD_OK) return 4;
    file=fopen(argv[2],"rb"); if(file==NULL) return 5;
    bytes=fread(binding_bytes,1u,sizeof(binding_bytes),file); fclose(file);
    if(bytes!=sizeof(binding_bytes)) return 6;
    for(i=0u;i<256u;i++) for(j=0u;j<4u;j++) {
        const unsigned char *p=&binding_bytes[(i*4u+j)*8u];
        bindings[i].images[j].material=word(p); bindings[i].images[j].width=word(p+2);
        bindings[i].images[j].height=word(p+4); bindings[i].images[j].flags=word(p+6);
    }
    for(z=0u;z<(uint32_t)level.settings[2];z++) for(x=0u;x<(uint32_t)level.settings[0];x++) {
        if(dok_geometry_build_cell(&world,bindings,x,z,&candidate_cell,&cell)!=DOK_GEOMETRY_OK) return 7;
        for(i=0u;i<cell.count;i++) {
            const dok_geometry_quad *q=&cell.quads[i];
            printf("%u %u %u %u %u %u %u %u %u",(unsigned)x,(unsigned)z,(unsigned)i,(unsigned)q->kind,(unsigned)q->orientation,
                (unsigned)q->source_glyph,(unsigned)q->material,(unsigned)q->image_flags,(unsigned)q->object_flags);
            for(k=0u;k<4u;k++) printf(" %" PRId32 " %" PRId32 " %" PRId32 " %" PRId32 " %" PRId32,
                q->vertices[k].x,q->vertices[k].y,q->vertices[k].z,q->vertices[k].u_q16,q->vertices[k].v_q16);
            putchar('\n');
        }
    }
    return 0;
}
