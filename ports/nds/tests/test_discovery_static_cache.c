#include "dok/map_discovery.h"
#include <stdio.h>
#include <string.h>
extern dok_status baseline_dok_map_discovery_update(const dok_initial_world *,const dok_geometry_binding [256],
    const dok_sprite_list *,const uint16_t *,const dok_nds_render_camera *,uint32_t,
    dok_map_discovery_workspace *,unsigned char [4096],dok_map_discovery_stats *);
static dok_map_discovery_workspace old_work,new_work;
static dok_level_definition definition,candidate;
static dok_initial_world world,world_candidate;
static dok_geometry_binding bindings[256];
static dok_sprite_list sprites;
static dok_map_static_cache cache;
static unsigned char source[65536],wire[8192],old_visit[4096],new_visit[4096];
static uint16_t flags[256];
static uint32_t rng=0x638c41f7u,cases,hits,overflows;
static uint32_t random32(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static unsigned word(const unsigned char *p){return (unsigned)p[0]|(unsigned)p[1]<<8;}
extern dok_status baseline_dok_map_record_geometry(const dok_geometry_quad *,const dok_nds_render_camera *,dok_map_render_record *);
static int32_t signed_bits(uint32_t n){return n<=INT32_MAX?(int32_t)n:-1-(int32_t)(UINT32_MAX-n);}
static int projection_vectors(void)
{
    /* Exercise low-DWORD overflow, the near plane, both horizontal kinds and
     * arbitrary public quads that must retain independent transforms. */
    for(unsigned n=0;n<20000u;++n){
        dok_geometry_quad q={0};dok_nds_render_camera c={0};dok_map_render_record a,b;
        int32_t x=signed_bits(random32()),z=signed_bits(random32());
        int32_t bx=signed_bits((uint32_t)x+(random32()%961u)),az=signed_bits((uint32_t)z-(random32()%961u));
        c.x=signed_bits(random32());c.z=signed_bits(random32());
        if(!dok_render_trig(n%361u,&c.sin_q15,&c.cos_q15))return 0;
        q.kind=n&1u?DOK_GEOMETRY_FLOOR_QUAD:DOK_GEOMETRY_CEILING_QUAD;
        q.cell_x=n%64u;q.cell_z=(n/64u)%64u;q.object_flags=(uint16_t)random32();
        q.vertices[0].x=q.vertices[3].x=x;q.vertices[1].x=q.vertices[2].x=bx;
        q.vertices[0].z=q.vertices[1].z=z;q.vertices[2].z=q.vertices[3].z=az;
        if(n%3u==0u)q.vertices[2].x=signed_bits((uint32_t)bx+1u);
        memset(&a,0xa5,sizeof(a));b=a;
        dok_status sa=baseline_dok_map_record_geometry(&q,&c,&a),sb=dok_map_record_geometry(&q,&c,&b);
        if(sa!=sb||memcmp(&a,&b,sizeof(a)))return 0;
    }
    return 1;
}
int main(int argc,char **argv)
{
    if(!projection_vectors())return 20;
    if(argc!=3)return 1;
    FILE *file=fopen(argv[1],"rb");if(!file)return 2;
    size_t bytes=fread(source,1,sizeof(source),file);fclose(file);
    dok_asset_view view={0};view.entry.language=1;view.entry.representation=DOK_ASSET_LEVEL_CP866;
    view.entry.bytes=(uint32_t)bytes;view.data=source;
    if(dok_level_parse(&view,&candidate,&definition)!=DOK_WORLD_OK||
       dok_initial_world_build(&definition,1,&world_candidate,&world)!=DOK_WORLD_OK)return 3;
    file=fopen(argv[2],"rb");if(!file)return 4;
    bytes=fread(wire,1,sizeof(wire),file);fclose(file);if(bytes!=sizeof(wire))return 5;
    for(unsigned i=0;i<256u;++i)for(unsigned j=0;j<4u;++j){const unsigned char *p=wire+(i*4u+j)*8u;
        bindings[i].images[j]=(dok_geometry_image){(uint16_t)word(p),(uint16_t)word(p+2),(uint16_t)word(p+4),(uint16_t)word(p+6)};}
    for(unsigned n=0;n<256u;++n){
        dok_nds_render_camera camera={0};unsigned heading=random32()%361u;
        camera.x=(int32_t)(random32()%30720u);camera.z=(int32_t)(random32()%30720u);camera.y=-300;
        if(!dok_render_trig(heading,&camera.sin_q15,&camera.cos_q15))return 6;
        for(unsigned i=0;i<256u;++i)flags[i]=(uint16_t)random32();
        memset(&cache,0,sizeof(cache));
        for(unsigned step=0;step<8u;++step){
            if(step==4u)flags[n%256u]^=1u;
            if(step==5u)++world.epoch;
            if(step==6u)++camera.x;
            if(step==7u)cache.valid=0;
            const uint16_t *runtime_flags=n%3u?flags:NULL;
            sprites.world_epoch=world.epoch;sprites.count=(n+step)%7u==0u?512u:(n+step)%64u;
            for(unsigned i=0;i<sprites.count;++i){dok_sprite_pose *p=&sprites.poses[i];
                unsigned cell=random32()%4096u;
                if(n&1u)cell=(unsigned)(camera.z/480)*64u+(unsigned)(camera.x/480);
                p->x=(int32_t)(cell&63u)*480+(int32_t)(random32()%481u);
                p->z=(int32_t)(cell>>6)*480+(int32_t)(random32()%481u);p->y=-300;
                p->width=(int16_t)((int32_t)(random32()%481u)-240);p->height=(int16_t)(random32()%481u);
                p->flags=(uint16_t)random32();p->source_id=(i%11u==0u?0x90000000u:0x80000000u|((random32()&1u)<<24))|cell;
            }
            const dok_sprite_list *list=step==2u?NULL:&sprites;
            for(unsigned i=0;i<4096u;++i)old_visit[i]=new_visit[i]=(unsigned char)(random32()&1u);
            dok_map_discovery_stats a,b;memset(&a,0xa5,sizeof(a));memset(&b,0xa5,sizeof(b));
            memset(&old_work,0,sizeof(old_work));memset(&new_work,0,sizeof(new_work));
            uint32_t before_hits=cache.hits;
            dok_status sa=baseline_dok_map_discovery_update(&world,bindings,list,runtime_flags,&camera,heading,&old_work,old_visit,&a);
            dok_status sb=dok_map_discovery_update_cached(&world,bindings,list,runtime_flags,&camera,heading,&new_work,new_visit,&b,&cache);
            if(sa!=sb||memcmp(&a,&b,sizeof(a))||memcmp(old_visit,new_visit,sizeof(old_visit))||
               old_work.count!=new_work.count||memcmp(old_work.records,new_work.records,new_work.count*sizeof(dok_map_render_record))||
               memcmp(old_work.order,new_work.order,new_work.count*sizeof(uint16_t))){
                fprintf(stderr,"Mismatch pose%u step%u cache%u statuses%u/%u\n",n,step,cache.valid,(unsigned)sa,(unsigned)sb);return 7;}
            hits+=cache.hits-before_hits;overflows+=!cache.valid&&sb==DOK_STATUS_OK;++cases;
        }
    }
    if(DOK_MAP_STATIC_RECORDS>=256u&&!hits)return 8;
    if(DOK_MAP_STATIC_RECORDS<=8u&&!overflows)return 9;
    printf("{\"passed\":true,\"projection_vectors\":20000,\"cases\":%u,\"hits\":%u,\"overflow_fallbacks\":%u,\"cache_bytes\":%u}\n",cases,hits,overflows,(unsigned)sizeof(cache));
    return 0;
}
