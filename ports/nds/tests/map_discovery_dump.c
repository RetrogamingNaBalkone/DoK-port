#include "dok/map_discovery.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
static dok_map_discovery_workspace workspace;
static dok_level_definition definition,candidate_definition;
static dok_initial_world world,candidate_world;
static dok_geometry_binding bindings[256];
static unsigned char source[65536],binding_bytes[8192];
static unsigned word(const unsigned char *p){return (unsigned)p[0]|(unsigned)p[1]<<8;}
int main(int argc,char **argv)
{
    if(argc==2 && strcmp(argv[1],"--sprite")==0){
        dok_nds_render_camera camera={0};dok_sprite_pose pose={0};int width,height;unsigned source_id,flags;
        while(scanf("%" SCNd32 " %" SCNd32 " %" SCNd32 " %" SCNd32 " %" SCNd32
            " %" SCNd32 " %" SCNd32 " %" SCNd32 " %d %d %u %u",
            &camera.x,&camera.y,&camera.z,&camera.sin_q15,&camera.cos_q15,&pose.x,&pose.y,&pose.z,
            &width,&height,&source_id,&flags)==12){
            dok_map_render_record record,before;dok_status status;
            memset(&record,0xa5,sizeof(record));before=record;
            pose.width=(int16_t)width;pose.height=(int16_t)height;pose.source_id=source_id;pose.flags=(uint16_t)flags;
            status=dok_map_record_sprite(&pose,&camera,&record);
            if(status!=DOK_STATUS_OK){if(memcmp(&record,&before,sizeof(record)))return 20;printf("%u\n",(unsigned)status);continue;}
            printf("0 %u %" PRId32 " %d %d %u\n",(unsigned)record.flags,record.key,record.left,record.right,(unsigned)record.cell);
        }
        return 0;
    }
    if(argc==4 && (strcmp(argv[1],"--world")==0 || strcmp(argv[1],"--world-extra")==0)) {
        FILE *file;size_t bytes;dok_asset_view view={0};uint32_t i,j,heading;
        dok_nds_render_camera camera={0};dok_map_discovery_stats stats;unsigned char visited[4096];
        file=fopen(argv[2],"rb");if(!file)return 10;bytes=fread(source,1,sizeof(source),file);fclose(file);
        view.entry.language=1;view.entry.representation=DOK_ASSET_LEVEL_CP866;view.entry.bytes=(uint32_t)bytes;view.data=source;
        if(dok_level_parse(&view,&candidate_definition,&definition)!=DOK_WORLD_OK)return 11;
        if(dok_initial_world_build(&definition,1,&candidate_world,&world)!=DOK_WORLD_OK)return 12;
        file=fopen(argv[3],"rb");if(!file)return 13;bytes=fread(binding_bytes,1,sizeof(binding_bytes),file);fclose(file);
        if(bytes!=sizeof(binding_bytes))return 14;
        for(i=0;i<256;++i)for(j=0;j<4;++j){const unsigned char *p=binding_bytes+(i*4u+j)*8u;
            bindings[i].images[j]=(dok_geometry_image){(uint16_t)word(p),(uint16_t)word(p+2),(uint16_t)word(p+4),(uint16_t)word(p+6)};}
        while(scanf("%" SCNd32 " %" SCNd32 " %" SCNd32 " %" SCNu32,&camera.x,&camera.y,&camera.z,&heading)==4){
            if(!dok_render_trig(heading,&camera.sin_q15,&camera.cos_q15))return 15;
            memset(visited,0,sizeof(visited));
            dok_sprite_list extra={0};extra.count=1;extra.world_epoch=1;extra.revision=1;
            extra.poses[0]=(dok_sprite_pose){.x=camera.x,.y=camera.y,.z=camera.z+480,.width=96,.height=96,
                .flags=8,.source_id=UINT32_C(0x90000011)};
            if(dok_map_discovery_update(&world,bindings,strcmp(argv[1],"--world-extra")==0?&extra:NULL,NULL,
                &camera,heading,&workspace,visited,&stats)!=DOK_STATUS_OK)return 16;
            printf("%u ",(unsigned)workspace.count);
            for(i=0;i<workspace.count;++i){const dok_map_render_record *r=&workspace.records[i];
                printf("%u %" PRId32 " %d %d %u %u ",(unsigned)r->flags,r->key,r->left,r->right,(unsigned)r->cell,(unsigned)workspace.order[i]);}
            for(i=0;i<4096;++i)putchar(visited[i]?'1':'0');putchar('\n');
        }
        return 0;
    }
    if(argc==2 && strcmp(argv[1],"--bounds")==0) {
        dok_nds_render_camera camera={0};dok_render_gather_bounds bounds;uint32_t heading;
        definition.settings[DOK_LEVEL_WIDTH]=64;definition.settings[DOK_LEVEL_LENGTH]=64;definition.settings[DOK_LEVEL_CELL_SIDE]=480;
        world.definition=&definition;
        while(scanf("%" SCNd32 " %" SCNd32 " %" SCNu32,&camera.x,&camera.z,&heading)==3){
            if(!dok_render_trig(heading,&camera.sin_q15,&camera.cos_q15))return 17;
            if(dok_map_discovery_bounds(&world,&camera,heading,&bounds)!=DOK_STATUS_OK)return 18;
            printf("%" PRId32 " %" PRId32 " %" PRId32 " %" PRId32 "\n",bounds.min_x,bounds.min_z,bounds.max_x,bounds.max_z);
        }
        return 0;
    }
    if(argc==2 && strcmp(argv[1],"--records")==0) {
        unsigned char visited[4096]={0};dok_map_discovery_stats stats;uint32_t count,i;
        while(scanf("%" SCNu32,&count)==1) {
            if(count>DOK_MAP_DISCOVERY_RECORDS)return 2;
            memset(visited,0,sizeof(visited));workspace.count=count;
            for(i=0;i<count;++i){int left,right;unsigned cell,flags;
                if(scanf("%" SCNd32 " %d %d %u %u",&workspace.records[i].key,&left,&right,&cell,&flags)!=5)return 3;
                workspace.records[i].left=(int16_t)left;workspace.records[i].right=(int16_t)right;
                workspace.records[i].cell=(uint16_t)cell;workspace.records[i].flags=(uint16_t)flags;
            }
            if(dok_map_discovery_records(&workspace,visited,&stats)!=DOK_STATUS_OK)return 4;
            for(i=0;i<count;++i)printf("%u %u %" PRId32 " ",(unsigned)workspace.order[i],
                (unsigned)workspace.records[i].flags,workspace.records[i].key);
            for(i=0;i<4096u;++i)putchar(visited[i]?'1':'0');putchar('\n');
        }
        return 0;
    }
    for(;;) {
        dok_geometry_quad quad={0};dok_nds_render_camera camera={0};dok_map_render_record record;
        unsigned kind,cell_x,cell_z,flags,i;
        if(scanf("%u %u %u %u",&kind,&cell_x,&cell_z,&flags)!=4)break;
        quad.kind=(uint8_t)kind;quad.cell_x=(uint8_t)cell_x;quad.cell_z=(uint8_t)cell_z;quad.object_flags=(uint16_t)flags;
        for(i=0;i<4u;++i)if(scanf("%" SCNd32 " %" SCNd32 " %" SCNd32,&quad.vertices[i].x,&quad.vertices[i].y,&quad.vertices[i].z)!=3)return 5;
        if(scanf("%" SCNd32 " %" SCNd32 " %" SCNd32 " %" SCNd32 " %" SCNd32,
            &camera.x,&camera.y,&camera.z,&camera.sin_q15,&camera.cos_q15)!=5)return 6;
        if(dok_map_record_geometry(&quad,&camera,&record)!=DOK_STATUS_OK)return 7;
        printf("%u %" PRId32 " %d %d %u\n",(unsigned)record.flags,record.key,record.left,record.right,(unsigned)record.cell);
    }
    return 0;
}
