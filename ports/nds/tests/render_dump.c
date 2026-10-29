#include "dok/render_materials.h"
#include "dok/render_scene.h"
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
static dok_render_scene scene;
static unsigned char asset_pool[DOK_NDS_ASSETS_BYTES];
static dok_geometry_binding backup_bindings[256];
static unsigned checks;
#define CHECK(c) do {checks++;if(!(c)){fprintf(stderr,"render_dump check:%d\n",__LINE__);return 0;}}while(0)

static void hex(const unsigned char *p,uint32_t bytes)
{uint32_t i;for(i=0u;i<bytes;i++) printf("%02x",(unsigned)p[i]);}

static int number(const char *text,int32_t *out)
{
    char *end; long long value=strtoll(text,&end,10);
    if(*text=='\0' || *end!='\0' || value<INT32_MIN || value>INT32_MAX) return 0;
    *out=(int32_t)value; return 1;
}

static int package_is_exact(const char *path)
{
    static const unsigned char expected[32]={0x2f,0x03,0xba,0x02,0xc9,0x98,0x6b,0x4e,
      0x0b,0xd3,0x21,0xe9,0x4c,0xa0,0x0d,0xf8,0xa3,0x55,0xf7,0xdb,0xb1,0xdf,0xf5,0x59,
      0x8a,0x95,0x0d,0x33,0x66,0x06,0x15,0x11};
    dok_sha256 hash; unsigned char actual[32]; size_t count,total=0u;
    FILE *file=fopen(path,"rb");
    if(file==NULL) return 0;
    dok_sha256_begin(&hash);
    while((count=fread(io.io,1u,sizeof(io.io),file))!=0u) {
        total+=count;dok_sha256_update(&hash,io.io,(uint32_t)count);
    }
    if(ferror(file)) {fclose(file);return 0;}
    fclose(file);dok_sha256_end(&hash,actual);
    return total==9230016u && memcmp(actual,expected,32u)==0;
}

static void entry_json(const dok_asset_entry *entry)
{
    printf("{\"id\":%" PRIu32 ",\"source_index\":%u,\"bytes\":%" PRIu32 ",\"width\":%u,\"height\":%u,\"storage_width\":%u,\"storage_height\":%u,\"param0\":%" PRIu32 ",\"sha256\":\"",
      entry->id,(unsigned)entry->source_index,entry->bytes,(unsigned)entry->width,(unsigned)entry->height,
      (unsigned)entry->storage_width,(unsigned)entry->storage_height,entry->param0);
    hex(entry->sha256,32u);printf("\",\"name_cp866\":\"");hex(entry->name_cp866,16u);printf("\"}");
}

static void world_json(void)
{
    uint32_t i,j,first=1u;
    printf("{\"asset_bytes\":%" PRIu32 ",\"resident_world_bytes\":%u,\"workspace_bytes\":%u,\"scene_bytes\":%u,\"materials\":[",
      world.asset_bytes,(unsigned)sizeof(world),(unsigned)sizeof(workspace),(unsigned)sizeof(scene));
    for(i=0u;i<world.material_count;i++) {
        unsigned char hash_bytes[32];dok_sha256 hash;
        const dok_render_material *m=&world.materials[i];
        if(i) putchar(',');
        dok_sha256_begin(&hash);dok_sha256_update(&hash,m->pixels,m->entry.bytes);dok_sha256_end(&hash,hash_bytes);
        printf("{\"material\":%" PRIu32 ",\"color0_transparent\":%" PRIu32 ",\"pixels_sha256\":\"",i+1u,m->color0_transparent);
        hex(hash_bytes,32u);printf("\",\"entry\":");entry_json(&m->entry);putchar('}');
    }
    printf("],\"bindings\":[");
    for(i=0u;i<256u;i++) for(j=0u;j<4u;j++) {
        const dok_geometry_image *b=&world.bindings[i].images[j];
        if(b->material==0u) continue;
        if(!first) putchar(',');
        first=0u;
        printf("{\"glyph\":%" PRIu32 ",\"slot\":%" PRIu32 ",\"material\":%u,\"width\":%u,\"height\":%u,\"flags\":%u}",
          i,j,(unsigned)b->material,(unsigned)b->width,(unsigned)b->height,(unsigned)b->flags);
    }
    printf("]}");
}

static int camera_set(dok_nds_render_camera *camera,int32_t x,int32_t y,int32_t z,uint32_t heading)
{
    camera->x=x;camera->y=y;camera->z=z;
    return dok_render_trig(heading,&camera->sin_q15,&camera->cos_q15);
}

static void counts_json(void)
{
    const dok_render_scene_counts *c=&scene.counts;
    printf("{\"gathered_cells\":%" PRIu32 ",\"source_quads\":%" PRIu32 ",\"frustum_rejected\":%" PRIu32 ",\"published_quads\":%" PRIu32 ",\"merge_candidates\":%" PRIu32 ",\"merged_quads\":%" PRIu32 ",\"bounds\":[%" PRId32 ",%" PRId32 ",%" PRId32 ",%" PRId32 "]}",
      c->gathered_cells,c->source_quads,c->frustum_rejected,c->published_quads,c->merge_candidates,c->merged_quads,
      c->bounds.min_x,c->bounds.min_z,c->bounds.max_x,c->bounds.max_z);
}

static int lifecycle(void)
{
    const dok_nds_render_frame *frame=NULL,*unchanged;
    dok_nds_render_camera camera;uint32_t generation,i;
    dok_render_gather_bounds bounds,old_bounds;
    CHECK(camera_set(&camera,15120,-300,22800,90u));
    dok_render_scene_init(&scene);
    CHECK(dok_render_scene_prepare(&scene,&world.initial,world.bindings,&camera,90u,&frame)==DOK_RENDER_SCENE_OK);
    CHECK(frame!=NULL && scene.leased && frame->generation==1u);
    generation=frame->generation;unchanged=frame;
    CHECK(dok_render_scene_prepare(&scene,&world.initial,world.bindings,&camera,90u,&frame)==DOK_RENDER_SCENE_BUSY);
    CHECK(frame==unchanged && scene.generation==generation && scene.leased);
    CHECK(dok_render_scene_release(&scene,generation+1u)==DOK_RENDER_SCENE_ARGUMENT);
    CHECK(scene.leased);
    CHECK(dok_render_scene_release(&scene,generation)==DOK_RENDER_SCENE_OK);
    CHECK(!scene.leased);
    CHECK(dok_render_scene_release(&scene,generation)==DOK_RENDER_SCENE_ARGUMENT);
    scene.generation=UINT32_MAX;
    CHECK(dok_render_scene_prepare(&scene,&world.initial,world.bindings,&camera,90u,&frame)==DOK_RENDER_SCENE_LIMIT);
    CHECK(frame==unchanged && !scene.leased);scene.generation=generation;
    memset(&bounds,0xa5,sizeof(bounds));old_bounds=bounds;
    camera.sin_q15++;
    CHECK(dok_render_bounds(&world.initial,&camera,90u,&bounds)==DOK_RENDER_SCENE_ARGUMENT);
    CHECK(memcmp(&bounds,&old_bounds,sizeof(bounds))==0);camera.sin_q15--;
    memcpy(backup_bindings,world.bindings,sizeof(backup_bindings));
    for(i=0u;i<256u;i++) memset(&world.bindings[i],0,sizeof(world.bindings[i]));
    CHECK(dok_render_scene_prepare(&scene,&world.initial,world.bindings,&camera,90u,&frame)==DOK_RENDER_SCENE_GEOMETRY);
    CHECK(frame==unchanged && !scene.leased && scene.generation==generation);
    memcpy(world.bindings,backup_bindings,sizeof(backup_bindings));
    return 1;
}

typedef struct Rank {uint32_t x,z,heading,quads,source,cells,rejected;} Rank;
static Rank best[5];
static int scan(uint32_t step)
{
    uint32_t x,z,h,visited=0u,max_source=0u,merge_candidates=0u,merged_quads=0u;
    dok_render_scene_init(&scene);memset(best,0,sizeof(best));
    for(z=0u;z<64u;z+=step) for(x=0u;x<64u;x+=step) {
        uint8_t glyph=world.initial.maps[DOK_WORLD_TMAP][z*64u+x];
        uint16_t index=world.effective.glyph_index[glyph];
        if(index==0u || world.effective.objects[index-1u].class_id==2u) continue;
        for(h=0u;h<360u;h+=45u) {
            const dok_nds_render_frame *frame;dok_nds_render_camera camera;uint32_t i;
            Rank rank;
            if(!camera_set(&camera,(int32_t)x*480+240,-300,(int32_t)z*480+240,h)) return 0;
            if(dok_render_scene_prepare(&scene,&world.initial,world.bindings,&camera,h,&frame)!=DOK_RENDER_SCENE_OK) {
                fprintf(stderr,"scene scan failure at %u,%u h%u\n",(unsigned)x,(unsigned)z,(unsigned)h);return 0;
            }
            ++visited;if(scene.counts.source_quads>max_source) max_source=scene.counts.source_quads;
            merge_candidates+=scene.counts.merge_candidates;merged_quads+=scene.counts.merged_quads;
            rank=(Rank){x,z,h,scene.counts.published_quads,scene.counts.source_quads,frame->cell_count,scene.counts.frustum_rejected};
            for(i=0u;i<5u;i++) if(rank.quads>best[i].quads || (rank.quads==best[i].quads && rank.source>best[i].source)) {
                uint32_t j;for(j=4u;j>i;j--) best[j]=best[j-1u];best[i]=rank;break;
            }
            if(dok_render_scene_release(&scene,frame->generation)!=DOK_RENDER_SCENE_OK) return 0;
        }
    }
    printf("{\"step\":%" PRIu32 ",\"poses\":%" PRIu32 ",\"max_source_quads\":%" PRIu32 ",\"merge_candidates\":%" PRIu32 ",\"merged_quads\":%" PRIu32 ",\"top\":[",step,visited,max_source,merge_candidates,merged_quads);
    for(h=0u;h<5u;h++) {
        const Rank *r=&best[h];if(h) putchar(',');
        printf("{\"cell\":[%u,%u],\"camera\":[%u,-300,%u,%u],\"published_quads\":%u,\"source_quads\":%u,\"cells\":%u,\"frustum_rejected\":%u}",
          (unsigned)r->x,(unsigned)r->z,(unsigned)(r->x*480u+240u),(unsigned)(r->z*480u+240u),
          (unsigned)r->heading,(unsigned)r->quads,(unsigned)r->source,(unsigned)r->cells,(unsigned)r->rejected);
    }
    printf("]}");return 1;
}

static int scene_json(int32_t x,int32_t y,int32_t z,uint32_t heading)
{
    dok_nds_render_camera camera;const dok_nds_render_frame *frame;uint32_t i,j,k;
    if(!camera_set(&camera,x,y,z,heading)) return 0;
    dok_render_scene_init(&scene);
    if(dok_render_scene_prepare(&scene,&world.initial,world.bindings,&camera,heading,&frame)!=DOK_RENDER_SCENE_OK) return 0;
    printf("{\"counts\":");counts_json();printf(",\"generation\":%" PRIu32 ",\"cells\":[",frame->generation);
    for(i=0u;i<frame->cell_count;i++) {
        const dok_nds_render_cell *cell=&frame->cells[i];if(i) putchar(',');
        printf("{\"origin\":[%" PRId32 ",%" PRId32 ",%" PRId32 "],\"quads\":[",cell->origin_x,cell->origin_y,cell->origin_z);
        for(j=0u;j<cell->quad_count;j++) {
            const dok_nds_render_quad *q=&cell->quads[j];if(j) putchar(',');
            printf("{\"texture_slot\":%u,\"source_surface_id\":%" PRIu32 ",\"vertices\":[",(unsigned)q->texture_slot,q->source_surface_id);
            for(k=0u;k<4u;k++) {
                const dok_nds_render_vertex *v=&q->vertex[k];if(k) putchar(',');
                printf("[%" PRId32 ",%" PRId32 ",%" PRId32 ",%" PRId32 ",%" PRId32 "]",v->x,v->y,v->z,v->u_q16,v->v_q16);
            }
            printf("]}");
        }
        printf("]}");
    }
    printf("]}");return dok_render_scene_release(&scene,frame->generation)==DOK_RENDER_SCENE_OK;
}

int main(int argc,char **argv)
{
    dok_asset_reader reader;dok_asset_session session;NdsAssetFile file;
    int32_t level;
    if(argc<3 || !number(argv[2],&level) || level<1 || level>3) return 1;
    if(!package_is_exact(argv[1])) {fputs("Exact accepted N04 package required\n",stderr);return 2;}
    if(nds_asset_file_open(&file,argv[1],&reader)!=DOK_ASSET_OK) return 3;
    if(dok_assets_open(&reader,&io,&session)!=DOK_ASSET_OK) return 4;
    if(dok_render_world_load(&session,(uint32_t)level,(uint32_t)level,&world,&workspace,asset_pool,sizeof(asset_pool))!=DOK_STATUS_OK) {
        fputs("world load failed\n",stderr);nds_asset_file_close(&file);return 5;
    }
    if(argc==4 && strcmp(argv[3],"--bounds")==0) {
        int32_t x,z,h;
        while(scanf("%" SCNd32 " %" SCNd32 " %" SCNd32,&x,&z,&h)==3) {
            dok_nds_render_camera camera;dok_render_gather_bounds b;
            if(h<0 || !camera_set(&camera,x,-300,z,(uint32_t)h) || dok_render_bounds(&world.initial,&camera,(uint32_t)h,&b)!=DOK_RENDER_SCENE_OK) return 6;
            printf("%" PRId32 " %" PRId32 " %" PRId32 " %" PRId32 "\n",b.min_x,b.min_z,b.max_x,b.max_z);
        }
    } else {
        if(!lifecycle()) return 7;
        printf("{\"level\":%" PRId32 ",\"lifecycle_checks\":%u,\"world\":",level,checks);world_json();
        if(argc==5 && strcmp(argv[3],"--scan")==0) {
            int32_t step;if(!number(argv[4],&step) || step<1 || step>64) return 8;
            printf(",\"scan\":");if(!scan((uint32_t)step)) return 9;
        } else if(argc==8 && strcmp(argv[3],"--scene")==0) {
            int32_t x,y,z,h;
            if(!number(argv[4],&x)||!number(argv[5],&y)||!number(argv[6],&z)||!number(argv[7],&h)||h<0) return 10;
            printf(",\"scene\":");if(!scene_json(x,y,z,(uint32_t)h)) return 11;
        } else if(argc!=3) return 12;
        printf(",\"file_reads\":%" PRIu32 ",\"file_bytes_read\":%" PRIu32 ",\"max_request\":%" PRIu32 "}\n",file.calls,file.bytes_read,file.max_request);
    }
    nds_asset_file_close(&file);return 0;
}
