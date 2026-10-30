/* Host-only experiment: can the actual gameplay TMap mutations change base
 * geometry? Calls the current production builder; no cache implementation. */
#include "dok/geometry.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
static dok_level_definition level,level_scratch;
static dok_initial_world world,world_scratch;
static dok_geometry_binding bindings[256];
static dok_geometry_cell baseline[4096],candidate,actual;
static unsigned char source[65536],binding_bytes[8192],original[4096];
static uint32_t comparisons;
static unsigned word(const unsigned char *p){return p[0]|(unsigned)p[1]<<8;}
static unsigned class_at(unsigned glyph)
{return level.objects[level.glyph_index[glyph]-1u].class_id;}
static int compare_cell(uint32_t cell)
{
    if(dok_geometry_build_cell(&world,bindings,cell%64u,cell/64u,&candidate,&actual)!=DOK_GEOMETRY_OK)return 0;
    ++comparisons;
    return !memcmp(&actual,&baseline[cell],sizeof(actual));
}
static int compare_neighbours(uint32_t cell)
{
    uint32_t x=cell%64u,z=cell/64u;
    return compare_cell(cell)&&(!x||compare_cell(cell-1u))&&
        (x==63u||compare_cell(cell+1u))&&(!z||compare_cell(cell-64u))&&
        (z==63u||compare_cell(cell+64u));
}
int main(int argc,char **argv)
{
    FILE *file;size_t bytes;dok_asset_view view={0};uint32_t changes=0,mutable=0,corpses=0;
    if(argc!=3)return 1;
    file=fopen(argv[1],"rb");if(!file)return 2;
    bytes=fread(source,1,sizeof(source),file);fclose(file);
    view.entry.language=1;view.entry.representation=DOK_ASSET_LEVEL_CP866;
    view.entry.bytes=(uint32_t)bytes;view.data=source;
    if(dok_level_parse(&view,&level_scratch,&level)!=DOK_WORLD_OK||
       dok_initial_world_build(&level,1,&world_scratch,&world)!=DOK_WORLD_OK)return 3;
    if(level.settings[0]!=64||level.settings[2]!=64)return 4;
    file=fopen(argv[2],"rb");if(!file)return 5;
    bytes=fread(binding_bytes,1,sizeof(binding_bytes),file);fclose(file);
    if(bytes!=sizeof(binding_bytes))return 6;
    for(uint32_t i=0;i<256u;++i)for(uint32_t j=0;j<4u;++j){
        const unsigned char *p=binding_bytes+(i*4u+j)*8u;
        bindings[i].images[j]=(dok_geometry_image){(uint16_t)word(p),(uint16_t)word(p+2),
            (uint16_t)word(p+4),(uint16_t)word(p+6)};
    }
    memcpy(original,world.maps[DOK_WORLD_TMAP],sizeof(original));
    for(uint32_t cell=0;cell<4096u;++cell){
        if(dok_geometry_build_cell(&world,bindings,cell%64u,cell/64u,&candidate,&baseline[cell])!=DOK_GEOMETRY_OK)return 7;
    }
    for(uint32_t i=0;i<level.object_count;++i)corpses+=level.objects[i].class_id==9u;
    for(uint32_t cell=0;cell<4096u;++cell){
        unsigned kind=class_at(original[cell]);
        if(kind!=0u&&kind!=3u&&kind!=10u)continue;
        ++mutable;
        if(kind==3u||kind==10u){
            world.maps[DOK_WORLD_TMAP][cell]=' ';++changes;
            if(!compare_neighbours(cell))return 8;
        }
        /* Corpse writes are legal on class0; consumed pickups also become
         * class0. Every actual enemy glyph is tested, preserving its flags. */
        for(uint32_t i=0;i<level.object_count;++i)if(level.objects[i].class_id==9u){
            world.maps[DOK_WORLD_TMAP][cell]=level.objects[i].glyph;++changes;
            if(!compare_neighbours(cell))return 9;
        }
        world.maps[DOK_WORLD_TMAP][cell]=original[cell];
    }
    uint32_t corpse_index=0;
    for(uint32_t cell=0;cell<4096u;++cell){
        unsigned kind=class_at(original[cell]);
        if(kind!=0u&&kind!=3u&&kind!=10u)continue;
        if(cell&1u)world.maps[DOK_WORLD_TMAP][cell]=' ';
        else{
            while(level.objects[corpse_index].class_id!=9u)corpse_index=(corpse_index+1u)%level.object_count;
            world.maps[DOK_WORLD_TMAP][cell]=level.objects[corpse_index].glyph;
            corpse_index=(corpse_index+1u)%level.object_count;
        }
    }
    for(uint32_t cell=0;cell<4096u;++cell)if(!compare_cell(cell))return 10;
    printf("{\"passed\":true,\"mutable_initial_cells\":%u,\"enemy_glyphs\":%u,"
        "\"individual_mutations\":%u,\"full_cell_comparisons\":%u,\"joint_world_cells\":4096}\n",
        mutable,corpses,changes,comparisons);
    return 0;
}
