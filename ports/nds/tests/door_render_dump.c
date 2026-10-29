#include "dok/door_render.h"
#include "dok/render_materials.h"
#include "dok/sha256.h"
#include "../platform/assets/file_reader.h"
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static dok_render_world world;
static dok_render_workspace workspace;
static dok_asset_workspace io;
static dok_render_scene scene,backup; /* Host-only comparison, never ARM9. */
static dok_interaction_phase_view phases;
static unsigned char pixels[DOK_NDS_ASSETS_BYTES];
static uint32_t checks;
#define CHECK(c) do{++checks;if(!(c)){fprintf(stderr,"door_render_dump:%d\n",__LINE__);return 0;}}while(0)
static int number(const char *s,int32_t *out)
{ char *end;long long n=strtoll(s,&end,10);if(!*s || *end || n<INT32_MIN || n>INT32_MAX)return 0;*out=(int32_t)n;return 1; }
static int exact_package(const char *path)
{
    static const unsigned char expected[32]={0x2f,0x03,0xba,0x02,0xc9,0x98,0x6b,0x4e,
        0x0b,0xd3,0x21,0xe9,0x4c,0xa0,0x0d,0xf8,0xa3,0x55,0xf7,0xdb,0xb1,0xdf,0xf5,0x59,
        0x8a,0x95,0x0d,0x33,0x66,0x06,0x15,0x11};
    dok_sha256 hash;unsigned char actual[32];size_t n,total=0;FILE *f=fopen(path,"rb");
    if(!f)return 0;dok_sha256_begin(&hash);
    while((n=fread(io.io,1,sizeof(io.io),f))!=0u){total+=n;dok_sha256_update(&hash,io.io,(uint32_t)n);}
    if(ferror(f)){fclose(f);return 0;}fclose(f);dok_sha256_end(&hash,actual);
    return total==9230016u && memcmp(actual,expected,sizeof(actual))==0;
}
static void phase_set(int16_t phase,int16_t extent)
{
    uint32_t i;memset(&phases,0,sizeof(phases));phases.world_epoch=world.initial.epoch;phases.revision=1;
    for(i=0;i<world.effective.object_count;++i)if(world.effective.objects[i].class_id==7u) {
        uint32_t g=world.effective.objects[i].glyph;phases.phase[g]=phase;phases.extent[g]=extent;
    }
}
static int inventory(uint32_t verbose)
{
    dok_geometry_cell cell,candidate;
    uint32_t x,z,i,p,doors=0,cases=0,minw=32767,minh=32767,first=1;
    if(verbose)printf("{\"vectors\":[");
    for(z=0;z<(uint32_t)world.effective.settings[2];++z)for(x=0;x<(uint32_t)world.effective.settings[0];++x) {
        CHECK(dok_geometry_build_cell(&world.initial,world.bindings,x,z,&candidate,&cell)==DOK_GEOMETRY_OK);
        for(i=0;i<cell.count;++i) {
            const dok_geometry_quad *q=&cell.quads[i];uint32_t index=world.effective.glyph_index[q->source_glyph];
            const dok_geometry_image *image=&world.bindings[q->source_glyph].images[0];
            if(q->kind!=DOK_GEOMETRY_DOOR || world.effective.objects[index-1u].class_id!=7u)continue;
            ++doors;if(image->width<minw)minw=image->width;if(image->height<minh)minh=image->height;
            /* With reset UV and width>1 the edge test cannot merge adjacent
             * horizontal panels: a.rightU == b.leftU would require W-1=0.
             * A vertical orientation cannot share an X-run boundary. */
            CHECK(image->width>1u && image->height>1u);
            CHECK(q->vertices[0].u_q16==q->vertices[3].u_q16 && q->vertices[1].u_q16==q->vertices[2].u_q16);
            CHECK(q->vertices[0].u_q16!=q->vertices[1].u_q16);
            CHECK((image->flags&0xFFC0u)==0x40u || (image->flags&0xFFC0u)==0x100u);
            for(p=0;p<30u;++p) {
                dok_geometry_quad out;uint32_t v;
                dok_door_geometry_result result=dok_door_geometry_build(q,image,(int16_t)p,30,&out);
                CHECK(result==(p==29u?DOK_DOOR_GEOMETRY_HIDDEN:DOK_DOOR_GEOMETRY_KEEP));++cases;
                if(!p)CHECK(memcmp(q,&out,sizeof(out))==0);
                if(verbose) {
                    if(!first)putchar(',');first=0;
                    printf("{\"cell\":[%lu,%lu],\"ordinal\":%lu,\"glyph\":%u,\"width\":%u,\"height\":%u,\"flags\":%u,\"phase\":%lu,\"extent\":30,\"hidden\":%s,\"vertices\":[",
                        (unsigned long)x,(unsigned long)z,(unsigned long)i,q->source_glyph,image->width,image->height,image->flags,(unsigned long)p,p==29u?"true":"false");
                    if(p!=29u)for(v=0;v<4u;++v) {
                        const dok_geometry_vertex *a=&out.vertices[v];if(v)putchar(',');
                        printf("[%" PRId32 ",%" PRId32 ",%" PRId32 ",%" PRId32 ",%" PRId32 "]",a->x,a->y,a->z,a->u_q16,a->v_q16);
                    }
                    printf("]}");
                }
            }
        }
    }
    printf(verbose?"],":"{");
    printf("\"passed\":true,\"door_quads\":%lu,\"phase_cases\":%lu,\"minimum_width\":%lu,\"minimum_height\":%lu,\"checks\":%lu}\n",
        (unsigned long)doors,(unsigned long)cases,(unsigned long)minw,(unsigned long)minh,(unsigned long)checks);
    return 1;
}
static int compare_pose(int32_t x,int32_t y,int32_t z,uint32_t h)
{
    const dok_nds_render_frame *frame;
    dok_nds_render_camera camera={x,y,z,0,0};dok_door_render_counts counts;uint32_t i;
    CHECK(dok_render_trig(h,&camera.sin_q15,&camera.cos_q15));
    CHECK(dok_render_scene_prepare(&scene,&world.initial,world.bindings,&camera,h,&frame)==DOK_RENDER_SCENE_OK);
    backup=scene;
    CHECK(dok_render_scene_release(&scene,frame->generation)==DOK_RENDER_SCENE_OK);
    CHECK(dok_door_scene_prepare(&scene,&world.initial,world.bindings,&camera,h,&phases,&counts,&frame)==DOK_RENDER_SCENE_OK);
    CHECK(memcmp(&backup.counts,&scene.counts,sizeof(scene.counts))==0);
    CHECK(memcmp(backup.quads,scene.quads,scene.counts.published_quads*sizeof(scene.quads[0]))==0);
    CHECK(frame->cell_count==backup.frame.cell_count && !counts.cropped && !counts.hidden);
    for(i=0;i<frame->cell_count;++i) {
        const dok_nds_render_cell *a=&backup.cells[i],*b=&scene.cells[i];
        CHECK(a->origin_x==b->origin_x && a->origin_y==b->origin_y && a->origin_z==b->origin_z && a->quad_count==b->quad_count && a->quads==b->quads);
    }
    CHECK(dok_render_scene_release(&scene,frame->generation)==DOK_RENDER_SCENE_OK);
    return 1;
}
static int scan(uint32_t step)
{
    uint32_t x,z,h,poses=0;phase_set(0,1);dok_render_scene_init(&scene);
    for(z=0;z<64u;z+=step)for(x=0;x<64u;x+=step) {
        uint32_t glyph=world.initial.maps[DOK_WORLD_TMAP][z*64u+x];
        uint32_t index=world.effective.glyph_index[glyph];
        if(!index || world.effective.objects[index-1u].class_id==2u)continue;
        for(h=0;h<360u;h+=45u) {CHECK(compare_pose((int32_t)x*480+240,-300,(int32_t)z*480+240,h));++poses;}
    }
    printf("{\"passed\":true,\"poses\":%lu,\"checks\":%lu,\"step\":%lu}\n",(unsigned long)poses,(unsigned long)checks,(unsigned long)step);return 1;
}
static int scene_json(int32_t x,int32_t y,int32_t z,uint32_t h,int16_t phase,int16_t extent)
{
    const dok_nds_render_frame *frame;dok_nds_render_camera camera={x,y,z,0,0};
    dok_door_render_counts counts;uint32_t i,j,k;
    phase_set(phase,extent);dok_render_scene_init(&scene);
    CHECK(dok_render_trig(h,&camera.sin_q15,&camera.cos_q15));
    CHECK(dok_door_scene_prepare(&scene,&world.initial,world.bindings,&camera,h,&phases,&counts,&frame)==DOK_RENDER_SCENE_OK);
    printf("{\"passed\":true,\"doors\":%lu,\"cropped\":%lu,\"hidden\":%lu,\"cells\":[",(unsigned long)counts.doors,(unsigned long)counts.cropped,(unsigned long)counts.hidden);
    for(i=0;i<frame->cell_count;++i) {
        const dok_nds_render_cell *cell=&frame->cells[i];if(i)putchar(',');
        printf("{\"origin\":[%" PRId32 ",%" PRId32 ",%" PRId32 "],\"quads\":[",cell->origin_x,cell->origin_y,cell->origin_z);
        for(j=0;j<cell->quad_count;++j) {
            const dok_nds_render_quad *q=&cell->quads[j];if(j)putchar(',');
            printf("{\"source_surface_id\":%lu,\"texture_slot\":%u,\"vertices\":[",(unsigned long)q->source_surface_id,q->texture_slot);
            for(k=0;k<4u;++k) {
                const dok_nds_render_vertex *v=&q->vertex[k];if(k)putchar(',');
                printf("[%" PRId32 ",%" PRId32 ",%" PRId32 ",%" PRId32 ",%" PRId32 "]",v->x,v->y,v->z,v->u_q16,v->v_q16);
            }
            printf("]}");
        }
        printf("]}");
    }
    printf("]}\n");CHECK(dok_render_scene_release(&scene,frame->generation)==DOK_RENDER_SCENE_OK);return 1;
}
int main(int argc,char **argv)
{
    NdsAssetFile file;dok_asset_reader reader;dok_asset_session session;int32_t level;int result=0;
    if(argc<3 || !number(argv[2],&level) || level<1 || level>3)return 1;
    if(!exact_package(argv[1]))return 2;
    if(nds_asset_file_open(&file,argv[1],&reader)!=DOK_ASSET_OK)return 2;
    if(dok_assets_open(&reader,&io,&session)!=DOK_ASSET_OK)return 3;
    if(dok_render_world_load(&session,(uint32_t)level,(uint32_t)level,&world,&workspace,pixels,sizeof(pixels))!=DOK_STATUS_OK)return 4;
    if(argc==3)result=inventory(0);
    else if(argc==4 && strcmp(argv[3],"--geometry")==0)result=inventory(1);
    else if(argc==5 && strcmp(argv[3],"--scan")==0) {
        int32_t step;if(number(argv[4],&step) && step>=1 && step<=64)result=scan((uint32_t)step);
    }else if(argc==10 && strcmp(argv[3],"--scene")==0) {
        int32_t a[6];uint32_t i;for(i=0;i<6u && number(argv[i+4u],&a[i]);++i){}
        if(i==6u && a[3]>=0 && a[3]<=360 && a[4]>=0 && a[4]<32767 && a[5]>0 && a[5]<=32767)
            result=scene_json(a[0],a[1],a[2],(uint32_t)a[3],(int16_t)a[4],(int16_t)a[5]);
    }
    nds_asset_file_close(&file);return result?0:5;
}
