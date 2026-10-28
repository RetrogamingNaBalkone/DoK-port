#include "dok/collision.h"
#include "dok/rom_assets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
static dok_runtime world;
static uint32_t hash(const uint8_t *p,unsigned size) {uint32_t h=2166136261u;while(size--) h=(h^*p++)*16777619u;return h;}
int main(int argc,char **argv)
{
    if(argc!=3) return 2;
    FILE *f=fopen(argv[1],"rb");if(!f) return 3;
    if(fseek(f,0,SEEK_END)) return 4;
    long size=ftell(f);if(size<=0 || fseek(f,0,SEEK_SET)) return 4;
    uint8_t *bytes=malloc((size_t)size);if(!bytes) return 5;
    if(fread(bytes,1,(size_t)size,f)!=(size_t)size) return 6;
    fclose(f);dok_rom_assets package;dok_runtime_level level;
    if(dok_rom_open(&package,bytes,(size_t)size) || dok_rom_level(&level,&package,0,!strcmp(argv[2],"en")) ||
       dok_runtime_reset(&world,&level)) return 7;
    dok_runtime_actor *a=&world.actors[level.definitions[level.player_index-1].actor_index];dok_motion m={0};
    uint32_t commands,count;
    while(scanf("%"SCNu32" %"SCNu32,&commands,&count)==2) {
        if(count>128) return 8;
        for(unsigned i=0;i<count;++i) dok_motion_update(a,&m,commands,416,dok_collision_move,&world);
        printf("%"PRId32" %"PRId32" %"PRId32" %d %d %d %"PRId32" %"PRId32" %d %"PRId32" %"PRIu32" %"PRIu32" %"PRIu32"\n",
            a->x,a->y,a->z,a->heading,a->direction,a->step,a->accumulator_a,a->accumulator_b,
            a->command,a->step_distance,a->motion_flags,m.remembered_flags,hash(world.maps,3*level.cells));
    }
    free(bytes);return 0;
}
