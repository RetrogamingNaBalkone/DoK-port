#define main fixture_loader_main
#include "geometry_clear_equivalence.c"
#undef main
#include "dok/map_discovery.h"
#include <stdlib.h>
static uint64_t calls,scanned;
static dok_render_scene scene;
static dok_map_discovery_workspace workspace;
static unsigned char visited[DOK_LEVEL_MAX_CELLS];
const dok_level_field *counted_field_at(const dok_level_object *o,uint16_t offset)
{
    const dok_level_field *expected=NULL,*actual;uint32_t i;++calls;
    if(o && o->field_count<=DOK_LEVEL_MAX_FIELDS)
        for(i=0;i<o->field_count;++i){++scanned;if(o->fields[i].native_offset==offset){expected=&o->fields[i];break;}}
    actual=dok_level_field_at(o,offset);if(actual!=expected)abort();return actual;
}
int main(int argc,char **argv)
{
    FILE *f;size_t bytes;dok_asset_view view={0};uint32_t i,j,k;
    const int32_t poses[2][4]={{28560,-300,8880,45},{1200,-300,19440,315}};
    if(argc!=3)return 1;
    f=fopen(argv[1],"rb");if(!f)return 2;bytes=fread(source,1,sizeof(source),f);fclose(f);
    view.entry.language=1;view.entry.representation=DOK_ASSET_LEVEL_CP866;view.entry.bytes=(uint32_t)bytes;view.data=source;
    if(dok_level_parse(&view,&candidate_definition,&definition)!=DOK_WORLD_OK ||
       dok_initial_world_build(&definition,1,&candidate_world,&world)!=DOK_WORLD_OK)return 3;
    f=fopen(argv[2],"rb");if(!f)return 4;bytes=fread(binding_bytes,1,sizeof(binding_bytes),f);fclose(f);if(bytes!=8192)return 5;
    for(i=0;i<256;++i)for(j=0;j<4;++j){const unsigned char *p=binding_bytes+(i*4+j)*8;
        bindings[i].images[j]=(dok_geometry_image){(uint16_t)word(p),(uint16_t)word(p+2),(uint16_t)word(p+4),(uint16_t)word(p+6)};}
    for(k=0;k<2;++k){dok_nds_render_camera c={0};const dok_nds_render_frame *out=NULL;dok_map_discovery_stats stats;
        c.x=poses[k][0];c.y=poses[k][1];c.z=poses[k][2];dok_render_trig((uint32_t)poses[k][3],&c.sin_q15,&c.cos_q15);
        dok_render_scene_init(&scene);calls=scanned=0;
        if(dok_render_scene_prepare(&scene,&world,bindings,&c,(uint32_t)poses[k][3],&out)!=DOK_RENDER_SCENE_OK)return 6;
        printf("{\"pose\":%u,\"path\":\"scene\",\"calls\":%" PRIu64 ",\"scanned\":%" PRIu64 ",\"cells\":%u}\n",k,calls,scanned,scene.counts.gathered_cells);
        calls=scanned=0;
        if(dok_map_discovery_update(&world,bindings,NULL,NULL,&c,(uint32_t)poses[k][3],&workspace,visited,&stats)!=DOK_STATUS_OK)return 7;
        printf("{\"pose\":%u,\"path\":\"discovery\",\"calls\":%" PRIu64 ",\"scanned\":%" PRIu64 ",\"records\":%u}\n",k,calls,scanned,stats.records);
    }return 0;
}
