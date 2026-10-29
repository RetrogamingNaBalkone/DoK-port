#include "dok/sprite_assets.h"
#include "dok/render_materials.h"
#include "dok/door_render.h"
#include "dok/texture_cache.h"
#include "dok/sha256.h"
#include "../platform/assets/file_reader.h"
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
static dok_render_world world;
static dok_render_workspace scratch;
static dok_asset_workspace io;
static dok_sprite_asset_bank bank;
static dok_sprite_list list;
static dok_render_scene scene;
static unsigned char pool[DOK_NDS_ASSETS_BYTES];
static uint16_t slots[DOK_SPRITE_LIMIT];
static unsigned char cache_pool[65536];
static dok_texture_cache cache;
static unsigned checks;
#define CHECK(c) do { ++checks;if(!(c)) { fprintf(stderr,"sprite_dump:%d\n",__LINE__);return 0; } } while(0)
static int project(void)
{
    dok_nds_render_camera c;dok_sprite_pose p={0};int32_t w,h;
    while(scanf("%"SCNd32" %"SCNd32" %"SCNd32" %"SCNd32" %"SCNd32" %"SCNd32" %"SCNd32" %"SCNd32" %"SCNd32" %"SCNd32,
        &c.x,&c.y,&c.z,&c.sin_q15,&c.cos_q15,&p.x,&p.y,&p.z,&w,&h)==10) {
        dok_sprite_projection r;p.width=(int16_t)w;p.height=(int16_t)h;
        CHECK(dok_sprite_project(&c,&p,&r)==DOK_STATUS_OK);
        printf("%"PRId32" %"PRId32" %"PRId32" %"PRId32" %"PRId32" %"PRId32" %"PRId32" %lu\n",
            r.lateral,r.depth,r.left,r.right,r.top,r.bottom,r.sort_key,(unsigned long)r.visible);
    }
    return 1;
}
static int cache_checks(const dok_asset_session *session)
{
    uint32_t i,capacity;const dok_asset_entry *a=&bank.images[0],*b=&bank.images[1];
    dok_asset_entry invalid;unsigned char hash[32];dok_sha256 state;
    capacity=a->bytes>b->bytes?a->bytes:b->bytes;
    CHECK(capacity<=sizeof(cache_pool));
    dok_texture_cache_init(&cache,cache_pool,capacity);
    CHECK(dok_texture_cache_begin(&cache)==DOK_STATUS_OK);
    CHECK(dok_texture_cache_require(&cache,session,a)==DOK_STATUS_OK);
    CHECK(dok_texture_cache_require(&cache,session,b)==DOK_STATUS_NO_SPACE);
    CHECK(dok_texture_cache_pixels(&cache,a->id)!=NULL);
    CHECK(dok_texture_cache_begin(&cache)==DOK_STATUS_OK);
    CHECK(dok_texture_cache_require(&cache,session,b)==DOK_STATUS_OK && cache.evictions==1);
    invalid=*b;invalid.bytes*=2;
    CHECK(dok_texture_cache_require(&cache,session,&invalid)==DOK_STATUS_INVALID_ARGUMENT);
    dok_texture_cache_init(&cache,cache_pool,sizeof(cache_pool));
    for(i=0;i<bank.image_count;++i) {
        const dok_asset_entry *e=&bank.images[i];const unsigned char *p;
        CHECK(dok_texture_cache_begin(&cache)==DOK_STATUS_OK);
        CHECK(dok_texture_cache_require(&cache,session,e)==DOK_STATUS_OK);
        p=dok_texture_cache_pixels(&cache,e->id);CHECK(p!=NULL);
        dok_sha256_begin(&state);dok_sha256_update(&state,p,e->bytes);dok_sha256_end(&state,hash);
        CHECK(memcmp(hash,e->sha256,32)==0);
    }
    return 1;
}
static int inventory(const dok_asset_session *session)
{
    uint32_t i,j,frames=0,negative=0;uint64_t bytes=0;
    CHECK(cache_checks(session));
    printf("{\"images\":[");
    for(i=0;i<bank.image_count;++i) {
        const dok_asset_entry *e=&bank.images[i];if(i)putchar(',');bytes+=e->bytes;
        printf("[%lu,%u,%u,%u,%u,%lu]",(unsigned long)e->id,e->width,e->height,e->storage_width,e->storage_height,(unsigned long)e->bytes);
    }
    printf("],\"animations\":[");
    for(i=0;i<bank.animation_count;++i) {
        if(i)putchar(',');printf("{\"id\":%lu,\"frames\":[",(unsigned long)bank.animations[i]);
        for(j=0;j<bank.sequences[i].count;++j) {
            dok_sprite_frame f;dok_sprite_player p={0};uint32_t interval,cancel;
            CHECK(dok_sprite_animation_get(&bank.sequences[i],(int32_t)j,&f)==DOK_STATUS_OK);
            p.animation=bank.sequences[i];p.cursor=(int16_t)((int32_t)j-1);p.remaining=1;
            CHECK(dok_sprite_player_step(&p,&interval,&cancel)==DOK_STATUS_OK && !cancel);
            CHECK(f.image==p.displayed.image && f.interval==p.displayed.interval &&
                  f.parameter_a==p.displayed.parameter_a && f.parameter_b==p.displayed.parameter_b &&
                  f.width==p.displayed.width && f.height==p.displayed.height &&
                  interval==(uint32_t)(int32_t)f.interval);
            CHECK(dok_sprite_player_step(&p,&interval,&cancel)==DOK_STATUS_OK && cancel);
            if(j)putchar(',');++frames;negative+=f.interval<0;
            printf("[%lu,%d,%d,%d,%u,%u]",(unsigned long)f.image,f.interval,f.parameter_a,f.parameter_b,f.width,f.height);
        }
        printf("]}");
    }
    printf("],\"frames\":%lu,\"negative_intervals\":%lu,\"full_texture_bytes\":%"PRIu64",\"metadata_bytes\":%lu,\"checks\":%u}\n",
        (unsigned long)frames,(unsigned long)negative,bytes,(unsigned long)bank.metadata_bytes,checks);
    return 1;
}
static int prepare(int32_t x,int32_t z,uint32_t heading,uint32_t verbose)
{
    dok_nds_render_camera c={x,-300,z,0,0};const dok_sprite_list *out=NULL;
    dok_sprite_snapshot snapshot={world.initial.epoch,1,bank.bindings,&world.initial};
    const dok_nds_render_frame *frame;uint32_t i,base;
    CHECK(dok_render_trig(heading,&c.sin_q15,&c.cos_q15));
    CHECK(dok_sprites_gather(&snapshot,&c,heading,&list,&out)==DOK_STATUS_OK && out==&list);
    CHECK(dok_render_scene_prepare(&scene,&world.initial,world.bindings,&c,heading,&frame)==DOK_RENDER_SCENE_OK);
    base=scene.counts.published_quads;
    CHECK(dok_sprites_append(&scene,&list,slots,2)==DOK_RENDER_SCENE_ARGUMENT);
    CHECK(scene.counts.published_quads==base);
    CHECK(dok_sprites_append(&scene,&list,slots,1)==DOK_RENDER_SCENE_OK);
    CHECK(scene.counts.published_quads==base+list.count);
    if(verbose) {
        printf("{\"x\":%"PRId32",\"z\":%"PRId32",\"heading\":%lu,\"base_quads\":%lu,\"sprites\":[",x,z,(unsigned long)heading,(unsigned long)base);
        for(i=0;i<list.count;++i) {
            const dok_sprite_pose *p=&list.poses[i];if(i)putchar(',');
            printf("[%lu,%u,%"PRId32",%"PRId32",%"PRId32",%d,%d,%lu]",(unsigned long)p->source_id,p->glyph,p->x,p->y,p->z,p->width,p->height,(unsigned long)p->image);
        }
        printf("]}\n");
    }
    CHECK(dok_render_scene_release(&scene,frame->generation)==DOK_RENDER_SCENE_OK);
    return 1;
}
static int scan(uint32_t step)
{
    uint32_t x,z,h,max=0,poses=0,quads=0;int32_t best[3]={0};
    for(z=0;z<64u;z+=step)for(x=0;x<64u;x+=step) {
        uint32_t index=world.effective.glyph_index[world.initial.maps[DOK_WORLD_TMAP][z*64u+x]];
        if(!index || world.effective.objects[index-1u].class_id==2u)continue;
        for(h=0;h<360u;h+=45u) {
            CHECK(prepare((int32_t)x*480+240,(int32_t)z*480+240,h,0));++poses;
            if(list.count>max) { max=list.count;best[0]=(int32_t)x*480+240;best[1]=(int32_t)z*480+240;best[2]=(int32_t)h; }
            if(scene.counts.published_quads>quads)quads=scene.counts.published_quads;
        }
    }
    printf("{\"passed\":true,\"poses\":%lu,\"max_sprites\":%lu,\"max_quads\":%lu,\"heavy\":[%"PRId32",%"PRId32",%"PRId32"],\"checks\":%u}\n",
        (unsigned long)poses,(unsigned long)max,(unsigned long)quads,best[0],best[1],best[2],checks);
    return 1;
}
int main(int argc,char **argv)
{
    dok_asset_session session;dok_asset_reader reader;NdsAssetFile file;uint32_t level;int ok;
    if(argc==2 && !strcmp(argv[1],"--project"))return project()?0:1;
    if(argc<3)return 1;level=(uint32_t)strtoul(argv[2],NULL,10);
    if(nds_asset_file_open(&file,argv[1],&reader)!=DOK_ASSET_OK ||
       dok_assets_open(&reader,&io,&session)!=DOK_ASSET_OK)return 2;
    if(dok_render_world_load(&session,level,level,&world,&scratch,pool,sizeof(pool))!=DOK_STATUS_OK)return 3;
    if(dok_sprite_assets_load(&world.presentation,&bank,pool+world.asset_bytes,
        sizeof(pool)-world.asset_bytes)!=DOK_STATUS_OK)return 4;
    dok_render_scene_init(&scene);
    if(argc==3)ok=inventory(&session);
    else if(argc==5 && !strcmp(argv[3],"--scan"))ok=scan((uint32_t)strtoul(argv[4],NULL,10));
    else if(argc==4 && !strcmp(argv[3],"--poses")) {
        int32_t x,z;uint32_t h;ok=1;
        while(scanf("%"SCNd32" %"SCNd32" %"SCNu32,&x,&z,&h)==3)if(!prepare(x,z,h,1)) { ok=0;break; }
    } else ok=0;
    nds_asset_file_close(&file);return ok?0:5;
}
