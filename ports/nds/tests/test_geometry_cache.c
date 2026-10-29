#include "dok/geometry_cache.h"
#include "dok/render_materials.h"
#include "../platform/assets/file_reader.h"
#include <stdio.h>
#include <string.h>
static dok_render_world world;
static dok_render_workspace work;
static dok_asset_workspace io;
static unsigned char assets[DOK_NDS_ASSETS_BYTES];
static unsigned checks;
#define CHECK(e) do{++checks;if(!(e)){fprintf(stderr,"geometry_cache:%d\n",__LINE__);return 1;}}while(0)
int main(int argc,char **argv)
{
    NdsAssetFile file;dok_asset_reader reader;dok_asset_session session;
    CHECK(argc==2);CHECK(nds_asset_file_open(&file,argv[1],&reader)==DOK_ASSET_OK);
    CHECK(dok_assets_open(&reader,&io,&session)==DOK_ASSET_OK);
    for(uint32_t level=1;level<=3u;++level){
        CHECK(dok_render_world_load(&session,level,level,&world,&work,assets,sizeof(assets))==DOK_STATUS_OK);
        dok_geometry_cache_reset();dok_geometry_cache_begin(&world.initial,world.bindings);
        for(uint32_t pass=0;pass<3u;++pass)for(uint32_t z=0;z<64u;++z)for(uint32_t x=0;x<64u;++x){
            dok_geometry_cell ca,cb,a,b;
            CHECK(dok_geometry_build_cell_uncached(&world.initial,world.bindings,x,z,&ca,&a)==DOK_GEOMETRY_OK);
            CHECK(dok_geometry_build_cell(&world.initial,world.bindings,x,z,&cb,&b)==DOK_GEOMETRY_OK);
            CHECK(!memcmp(&a,&b,sizeof(a)));
            /* Mutation of the next cell affects both its centre and neighbors;
             * restoring it must not resurrect a stale pattern. */
            if(pass==1u && x>0u && x<63u && z>0u && z<63u){
                uint32_t at=z*64u+x;unsigned char old=world.initial.maps[DOK_WORLD_TMAP][at];
                world.initial.maps[DOK_WORLD_TMAP][at]=' ';
                CHECK(dok_geometry_build_cell_uncached(&world.initial,world.bindings,x-1u,z,&ca,&a)==DOK_GEOMETRY_OK);
                CHECK(dok_geometry_build_cell(&world.initial,world.bindings,x-1u,z,&cb,&b)==DOK_GEOMETRY_OK);
                CHECK(!memcmp(&a,&b,sizeof(a)));world.initial.maps[DOK_WORLD_TMAP][at]=old;
            }
        }
        for(uint32_t g=0;g<256u;++g)if(world.bindings[g].images[1].material){
            ++world.bindings[g].images[1].width;dok_geometry_cache_begin(&world.initial,world.bindings);
            dok_geometry_cell ca,cb,a,b;
            CHECK(dok_geometry_build_cell_uncached(&world.initial,world.bindings,31,31,&ca,&a)==DOK_GEOMETRY_OK);
            CHECK(dok_geometry_build_cell(&world.initial,world.bindings,31,31,&cb,&b)==DOK_GEOMETRY_OK);
            CHECK(!memcmp(&a,&b,sizeof(a)));--world.bindings[g].images[1].width;dok_geometry_cache_begin(&world.initial,world.bindings);
        }
    }
    nds_asset_file_close(&file);printf("{\"checks\":%u,\"failures\":0}\n",checks);return 0;
}
