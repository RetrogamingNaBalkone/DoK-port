#include "dok/effects.h"
#include "dok/render_materials.h"
#include "dok/sprite_assets.h"
#include "../platform/assets/file_reader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
static dok_render_world world;
static dok_render_workspace load_scratch;
static dok_light_workspace light_scratch;
static dok_asset_workspace io;
static dok_render_scene scene;
static dok_sprite_asset_bank bank;
static dok_sprite_list sprite_list;
static uint16_t sprite_slots[DOK_SPRITE_LIMIT];
static unsigned char pool[DOK_NDS_ASSETS_BYTES];
static unsigned checks;
#define CHECK(c) do{++checks;if(!(c)){fprintf(stderr,"effects_scene_dump:%d\n",__LINE__);return 0;}}while(0)
static int prepare(int32_t x,int32_t z,uint32_t heading,uint32_t verbose)
{
    dok_nds_render_camera c={x,-300,z,0,0};const dok_nds_render_frame *frame;dok_light_counts stats;
    uint32_t i,j,k;
    CHECK(dok_render_trig(heading,&c.sin_q15,&c.cos_q15));
    CHECK(dok_render_scene_prepare(&scene,&world.initial,world.bindings,&c,heading,&frame)==DOK_RENDER_SCENE_OK);
    const dok_sprite_list *list;dok_sprite_snapshot snapshot={world.initial.epoch,scene.generation,bank.bindings,&world.initial};
    CHECK(dok_sprites_gather(&snapshot,&c,heading,&sprite_list,&list)==DOK_STATUS_OK);
    CHECK(dok_sprites_append(&scene,list,sprite_slots,scene.generation)==DOK_RENDER_SCENE_OK);
    CHECK(dok_light_scene_apply(&scene,1920,&light_scratch,&stats)==DOK_RENDER_SCENE_OK);
    CHECK(scene.counts.published_quads<=DOK_RENDER_QUADS);
    if(verbose) {
        printf("{\"quads\":%lu,\"cells\":[",(unsigned long)scene.counts.published_quads);
        for(i=0;i<frame->cell_count;++i) {
            const dok_nds_render_cell *cell=&frame->cells[i];if(i)putchar(',');
            printf("{\"origin\":[%"PRId32",%"PRId32",%"PRId32"],\"quads\":[",cell->origin_x,cell->origin_y,cell->origin_z);
            for(j=0;j<cell->quad_count;++j) {
                const dok_nds_render_quad *q=&cell->quads[j];if(j)putchar(',');
                printf("{\"id\":%lu,\"row\":%u,\"texture_slot\":%u,\"vertices\":[",(unsigned long)q->source_surface_id,q->reserved,q->texture_slot);
                for(k=0;k<4u;++k) {const dok_nds_render_vertex *v=&q->vertex[k];if(k)putchar(',');
                    printf("[%"PRId32",%"PRId32",%"PRId32",%"PRId32",%"PRId32"]",v->x,v->y,v->z,v->u_q16,v->v_q16);}
                printf("]}");
            }
            printf("]}");
        }
        printf("]}\n");
    }
    CHECK(dok_render_scene_release(&scene,frame->generation)==DOK_RENDER_SCENE_OK);return 1;
}
int main(int argc,char **argv)
{
    NdsAssetFile file;dok_asset_reader reader;dok_asset_session session;uint32_t level;
    if(argc<3)return 1;
    level=(uint32_t)strtoul(argv[2],NULL,10);
    if(nds_asset_file_open(&file,argv[1],&reader)!=DOK_ASSET_OK || dok_assets_open(&reader,&io,&session)!=DOK_ASSET_OK)return 2;
    if(dok_render_world_load(&session,level,level,&world,&load_scratch,pool,sizeof(pool))!=DOK_STATUS_OK)return 3;
    uint32_t offset=(world.asset_bytes+31u)&~31u;
    if(dok_sprite_assets_load(&world.presentation,&bank,pool+offset,sizeof(pool)-offset)!=DOK_STATUS_OK)return 3;
    dok_render_scene_init(&scene);
    if(argc==4 && !strcmp(argv[3],"--poses")) {
        int32_t x,z;uint32_t h;
        while(scanf("%"SCNd32" %"SCNd32" %"SCNu32,&x,&z,&h)==3)if(!prepare(x,z,h,1))return 4;
    } else if(argc==5 && !strcmp(argv[3],"--scan")) {
        uint32_t step=(uint32_t)strtoul(argv[4],NULL,10),x,z,h,poses=0,max=0;int32_t best[3]={0};
        if(!step || step>64)return 5;
        for(z=0;z<64u;z+=step)for(x=0;x<64u;x+=step) {
            uint32_t index=world.effective.glyph_index[world.initial.maps[DOK_WORLD_TMAP][z*64u+x]];
            if(!index || world.effective.objects[index-1u].class_id==2u)continue;
            for(h=0;h<360u;h+=45u) {
                if(!prepare((int32_t)x*480+240,(int32_t)z*480+240,h,0))return 6;
                ++poses;if(scene.counts.published_quads>max) {max=scene.counts.published_quads;best[0]=(int32_t)x*480+240;best[1]=(int32_t)z*480+240;best[2]=(int32_t)h;}
            }
        }
        printf("{\"passed\":true,\"poses\":%lu,\"max_quads\":%lu,\"heavy\":[%"PRId32",%"PRId32",%"PRId32"],\"checks\":%u}\n",
            (unsigned long)poses,(unsigned long)max,best[0],best[1],best[2],checks);
    } else return 7;
    nds_asset_file_close(&file);return 0;
}
