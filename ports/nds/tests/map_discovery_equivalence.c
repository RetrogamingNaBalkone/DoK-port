#include "dok/map_discovery.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* Compile the archived accepted N20 source with renamed public functions.
 * The accepted workspace prefix ends at count; optimized indexes follow it. */
extern dok_status baseline_dok_map_discovery_update(const dok_initial_world *,
    const dok_geometry_binding [256],const dok_sprite_list *,const uint16_t *,
    const dok_nds_render_camera *,uint32_t,void *,unsigned char [4096],
    dok_map_discovery_stats *);
extern dok_status candidate4_dok_map_discovery_update(const dok_initial_world *,
    const dok_geometry_binding [256],const dok_sprite_list *,const uint16_t *,
    const dok_nds_render_camera *,uint32_t,void *,unsigned char [4096],
    dok_map_discovery_stats *);
extern dok_status candidate5_dok_map_discovery_update(const dok_initial_world *,
    const dok_geometry_binding [256],const dok_sprite_list *,const uint16_t *,
    const dok_nds_render_camera *,uint32_t,void *,unsigned char [4096],
    dok_map_discovery_stats *);
static union { uint64_t align; unsigned char bytes[35268]; } baseline;
static union { uint64_t align; unsigned char bytes[46532]; } candidate4;
static union { uint64_t align; unsigned char bytes[46532]; } candidate5;
static dok_map_discovery_workspace optimized;
static dok_level_definition definition,candidate_definition;
static dok_initial_world world,candidate_world;
static dok_geometry_binding bindings[256];
static unsigned char source[65536],binding_bytes[8192],before[4096],after[4096],middle[4096],compact[4096];
static dok_sprite_list sprites;
static uint16_t flags[256];
static uint32_t rng=UINT32_C(0x2170D15C);
static uint32_t random32(void)
{rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static unsigned word(const unsigned char *p){return (unsigned)p[0]|(unsigned)p[1]<<8;}

int main(int argc,char **argv)
{
    FILE *file;size_t bytes;dok_asset_view view={0};uint32_t i,j,n,cases=0;
    clock_t old_ticks=0,new_ticks=0;
    if(argc!=3)return 1;
    file=fopen(argv[1],"rb");if(!file)return 2;
    bytes=fread(source,1,sizeof(source),file);fclose(file);
    view.entry.language=1;view.entry.representation=DOK_ASSET_LEVEL_CP866;
    view.entry.bytes=(uint32_t)bytes;view.data=source;
    if(dok_level_parse(&view,&candidate_definition,&definition)!=DOK_WORLD_OK)return 3;
    if(dok_initial_world_build(&definition,1,&candidate_world,&world)!=DOK_WORLD_OK)return 4;
    file=fopen(argv[2],"rb");if(!file)return 5;
    bytes=fread(binding_bytes,1,sizeof(binding_bytes),file);fclose(file);
    if(bytes!=sizeof(binding_bytes))return 6;
    for(i=0;i<256u;++i)for(j=0;j<4u;++j) {
        const unsigned char *p=binding_bytes+(i*4u+j)*8u;
        bindings[i].images[j]=(dok_geometry_image){(uint16_t)word(p),
            (uint16_t)word(p+2),(uint16_t)word(p+4),(uint16_t)word(p+6)};
    }
    for(n=0;n<512u;++n) {
        dok_nds_render_camera camera={0};dok_map_discovery_stats a,b,c,d;
        dok_status sa,sb,sc,sd;uint32_t heading=random32()%361u;
        const dok_sprite_list *list=n%7u?&sprites:NULL;
        const uint16_t *runtime_flags=n%3u?flags:NULL;
        clock_t start;
        camera.x=(int32_t)(random32()%((uint32_t)definition.settings[DOK_LEVEL_WIDTH]*480u));
        camera.z=(int32_t)(random32()%((uint32_t)definition.settings[DOK_LEVEL_LENGTH]*480u));
        camera.y=-300;
        if(!dok_render_trig(heading,&camera.sin_q15,&camera.cos_q15))return 7;
        for(i=0;i<4096u;++i)before[i]=after[i]=middle[i]=compact[i]=(unsigned char)(random32()&1u);
        for(i=0;i<256u;++i)flags[i]=(uint16_t)random32();
        sprites.world_epoch=world.epoch;
        sprites.count=n%6u==0u?512u:n%6u==1u?0u:n%6u==2u?1u:
            n%6u==3u?31u:n%6u==4u?128u:random32()%513u;
        for(i=0;i<sprites.count;++i) {
            dok_sprite_pose *p=&sprites.poses[i];uint32_t cell=random32()%4096u;
            /* Dense near-camera cases stress repeated same-cell insertion,
             * both planes, arbitrary list order and synthetic provenance. */
            if(n&1u)cell=((uint32_t)camera.z/480u)*64u+(uint32_t)camera.x/480u;
            p->x=(int32_t)(cell&63u)*480+(int32_t)(random32()%481u);
            p->z=(int32_t)(cell>>6)*480+(int32_t)(random32()%481u);
            p->y=-300;p->width=(int16_t)((int32_t)(random32()%481u)-240);
            p->height=(int16_t)(random32()%481u);p->flags=(uint16_t)random32();
            p->source_id=(i%11u==0u?UINT32_C(0x90000000):
                UINT32_C(0x80000000)|((random32()&1u)<<24))|cell;
        }
        memset(&baseline,0,sizeof(baseline));memset(&optimized,0,sizeof(optimized));
        memset(&candidate4,0,sizeof(candidate4));
        memset(&candidate5,0,sizeof(candidate5));
        memset(&a,0xa5,sizeof(a));memset(&b,0xa5,sizeof(b));memset(&c,0xa5,sizeof(c));memset(&d,0xa5,sizeof(d));
        start=clock();sa=baseline_dok_map_discovery_update(&world,bindings,list,
            runtime_flags,&camera,heading,&baseline,before,&a);old_ticks+=clock()-start;
        start=clock();sb=dok_map_discovery_update(&world,bindings,list,
            runtime_flags,&camera,heading,&optimized,after,&b);new_ticks+=clock()-start;
        sc=candidate4_dok_map_discovery_update(&world,bindings,list,
            runtime_flags,&camera,heading,&candidate4,middle,&c);
        sd=candidate5_dok_map_discovery_update(&world,bindings,list,
            runtime_flags,&camera,heading,&candidate5,compact,&d);
        if(sa!=sb || memcmp(before,after,sizeof(before)) || memcmp(&a,&b,sizeof(a)) ||
            memcmp(baseline.bytes,&optimized,sizeof(baseline.bytes)) || sc!=sb ||
            memcmp(middle,after,sizeof(middle)) || memcmp(&c,&b,sizeof(c)) ||
            memcmp(candidate4.bytes,&optimized,sizeof(candidate4.bytes)) || sd!=sb ||
            memcmp(compact,after,sizeof(compact)) || memcmp(&d,&b,sizeof(d)) ||
            memcmp(candidate5.bytes,&optimized,sizeof(candidate5.bytes))) {
            fprintf(stderr,"Mismatch vector %u status %u/%u heading %u sprites %u\n",
                n,(unsigned)sa,(unsigned)sb,heading,sprites.count);return 8;
        }
        ++cases;
    }
    printf("{\"passed\":true,\"cases\":%u,\"baseline_workspace\":35268,"
        "\"optimized_workspace\":%u,\"host_baseline_ticks\":%" PRIuMAX ","
        "\"host_optimized_ticks\":%" PRIuMAX ",\"host_clock_per_second\":%" PRIuMAX "}\n",
        cases,(unsigned)sizeof(optimized),(uintmax_t)old_ticks,(uintmax_t)new_ticks,(uintmax_t)CLOCKS_PER_SEC);
    return 0;
}
