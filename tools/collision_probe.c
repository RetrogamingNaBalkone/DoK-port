#include "dok/collision.h"
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
/* Little-endian host test protocol, deliberately separate from G03 format.
 * 5 u16 settings (width,length,side,radius,glyph), 256 flags, 256 classes,
 * three maps; then op/u32, x/z/nx/nz/i32. op0=resolver,1=clear,
 * 2/3=one callback (distance=nx,heading=nz,strafe=op-2).
 * Output: x,z,clear, MMap FNV32, 256 door counters (int32). */
static dok_runtime world;
static dok_runtime_definition definitions[256];
static dok_runtime_level level;
int main(void)
{
#ifdef _WIN32
    _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
#endif
    uint16_t h[5],flags[256],classes[256];
    if(fread(h,sizeof h,1,stdin)!=1 || fread(flags,sizeof flags,1,stdin)!=1 ||
       fread(classes,sizeof classes,1,stdin)!=1) return 1;
    unsigned cells=(unsigned)h[0]*h[1];if(!cells || cells>4096) return 2;
    if(fread(world.maps,cells*3,1,stdin)!=1) return 3;
    level.cells=(uint16_t)cells;level.settings[DOK_LEVEL_WIDTH]=h[0];
    level.settings[DOK_LEVEL_LENGTH]=h[1];level.settings[DOK_LEVEL_CELL_SIDE]=h[2];
    level.settings[DOK_LEVEL_MIN_DISTANCE]=h[3];level.definitions=definitions;
    world.level=&level;world.object_count=256;
    for(unsigned i=0;i<256;++i) {level.glyph_index[i]=(uint16_t)(i+1);world.objects[i].flags=flags[i];definitions[i].class_id=classes[i];}
    int32_t v[5],out[260];
    while(fread(v,sizeof v,1,stdin)==1) {
        int32_t x=v[1],z=v[2];
        if(v[0]==0) dok_collision_resolve(&world,&x,&z,v[3],v[4]);
        else if(v[0]>=2) {
            dok_runtime_actor a={0};a.x=x;a.z=z;a.glyph=(uint8_t)h[4];a.heading=(int16_t)v[4];
            int16_t distance=(int16_t)v[3];
            if(v[0]>=4) {
                /* Explicit arithmetic quarter shift, including negatives. */
                distance=(int16_t)(distance>=0?distance/4:-((-distance+3)/4));
                for(unsigned i=0;i<3;++i) dok_collision_move(&a,distance,v[0]==5,&world);
            } else dok_collision_move(&a,distance,v[0]==3,&world);
            x=a.x;z=a.z;
        }
        out[0]=x;out[1]=z;out[2]=dok_collision_clear(&world,x,z,(int16_t)h[3]);
        uint32_t hash=2166136261u;for(unsigned i=0;i<cells;++i) hash=(hash^world.maps[i])*16777619u;
        out[3]=(int32_t)hash;
        for(unsigned i=0;i<256;++i) out[4+i]=world.objects[i].state.door.state_56;
        if(fwrite(out,sizeof out,1,stdout)!=1) return 4;
    }
    return ferror(stdin)?5:0;
}
