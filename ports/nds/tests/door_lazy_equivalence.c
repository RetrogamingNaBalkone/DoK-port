#include "dok/door_render.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
extern dok_render_scene_result baseline_dok_door_scene_prepare(dok_render_scene *,
    const dok_initial_world *,const dok_geometry_binding [256],const dok_nds_render_camera *,
    uint32_t,const dok_interaction_phase_view *,dok_door_render_counts *,const dok_nds_render_frame **);
static dok_render_scene old_scene,new_scene,old_normalized,new_normalized;
static dok_level_definition definition,candidate_definition;
static dok_initial_world world,candidate_world;
static dok_geometry_binding bindings[256];
static dok_interaction_phase_view phases;
static unsigned char source[65536],binding_bytes[8192];
static dok_nds_render_frame marker;
static uint32_t cases,errors,door_outputs,cropped_outputs,hidden_outputs;
static uint64_t bytes_compared;
static unsigned word(const unsigned char *p){return (unsigned)p[0]|(unsigned)p[1]<<8;}
static int compare(const dok_nds_render_camera *camera,uint32_t heading,uint32_t use_phases)
{
    const dok_nds_render_frame *a=&marker,*b=&marker;dok_door_render_counts ca,cb,sentinel;
    dok_render_scene_result sa,sb;uint32_t i;
    memset(&sentinel,0xa5,sizeof(sentinel));ca=cb=sentinel;
    sa=baseline_dok_door_scene_prepare(&old_scene,&world,bindings,camera,heading,use_phases?&phases:NULL,&ca,&a);
    sb=dok_door_scene_prepare(&new_scene,&world,bindings,camera,heading,use_phases?&phases:NULL,&cb,&b);
    if(sa!=sb || memcmp(&ca,&cb,sizeof(ca)))return 20;
    if(sa==DOK_RENDER_SCENE_OK) {
        if(a!=&old_scene.frame || b!=&new_scene.frame)return 21;
        door_outputs+=cb.doors;cropped_outputs+=cb.cropped;hidden_outputs+=cb.hidden;
    } else {
        if(a!=&marker || b!=&marker || memcmp(&cb,&sentinel,sizeof(cb)))return 22;
        ++errors;
    }
    old_normalized=old_scene;new_normalized=new_scene;
    for(i=0;i<DOK_RENDER_CELLS;++i) {
        const dok_nds_render_quad *x=old_scene.cells[i].quads,*y=new_scene.cells[i].quads;
        if((x==NULL)!=(y==NULL) || (x && x-old_scene.quads!=y-new_scene.quads))return 23;
        old_normalized.cells[i].quads=NULL;new_normalized.cells[i].quads=NULL;
    }
    if((old_scene.frame.cells==NULL)!=(new_scene.frame.cells==NULL) ||
        (old_scene.frame.cells && (old_scene.frame.cells!=old_scene.cells || new_scene.frame.cells!=new_scene.cells)))return 24;
    old_normalized.frame.cells=NULL;new_normalized.frame.cells=NULL;
    if(memcmp(&old_normalized,&new_normalized,sizeof(old_normalized))) {
        fprintf(stderr,"Door scene mismatch case %u status %u\n",cases,(unsigned)sa);return 25;
    }
    ++cases;bytes_compared+=sizeof(old_normalized);
    if(sa==DOK_RENDER_SCENE_OK &&
        (dok_render_scene_release(&old_scene,a->generation)!=DOK_RENDER_SCENE_OK ||
         dok_render_scene_release(&new_scene,b->generation)!=DOK_RENDER_SCENE_OK))return 26;
    return 0;
}
static void phase(int16_t p,int16_t extent)
{
    uint32_t i;memset(&phases,0,sizeof(phases));phases.world_epoch=world.epoch;phases.revision=1;
    for(i=0;i<256u;++i){phases.phase[i]=p;phases.extent[i]=extent;}
}
static void fixture(uint32_t degenerate,uint32_t mixed)
{
    uint32_t i;static const unsigned char glyphs[]={32,'F','D','E'};
    memset(&definition,0,sizeof(definition));memset(&world,0,sizeof(world));memset(bindings,0,sizeof(bindings));
    definition.language=1;definition.object_count=4;definition.settings[0]=3;definition.settings[1]=1;
    definition.settings[2]=3;definition.settings[3]=480;definition.settings[4]=960;
    for(i=0;i<4u;++i) {
        dok_level_object *o=&definition.objects[i];o->glyph=glyphs[i];definition.glyph_index[glyphs[i]]=(uint16_t)(i+1u);
        o->field_count=1;o->fields[0].native_offset=0x54;o->fields[0].kind=DOK_LEVEL_WORD;o->fields[0].value=10;
    }
    for(i=2;i<4u;++i){definition.objects[i].class_id=7;definition.objects[i].width_parameter=(int16_t)(degenerate?480:5);}
    world.definition=&definition;world.epoch=7;
    memset(world.maps[DOK_WORLD_TMAP],32,sizeof(world.maps[DOK_WORLD_TMAP]));
    memset(world.maps[DOK_WORLD_FCMAP],'F',sizeof(world.maps[DOK_WORLD_FCMAP]));
    for(i=3;i<6u;++i)world.maps[DOK_WORLD_TMAP][i]=(unsigned char)(mixed && i==4u?'E':'D');
    for(i=1;i<4u;++i)bindings['F'].images[i]=(dok_geometry_image){(uint16_t)i,1,1,0};
    bindings['D'].images[0]=bindings['E'].images[0]=(dok_geometry_image){4,(uint16_t)(degenerate?1:96),(uint16_t)(degenerate?1:128),0x48};
    dok_render_scene_init(&old_scene);dok_render_scene_init(&new_scene);
}
int main(int argc,char **argv)
{
    FILE *file;size_t bytes;dok_asset_view view={0};uint32_t i,j,heading,poses=0,focused=0;int result;
    dok_nds_render_camera camera={0};
    if(argc!=3)return 1;
    file=fopen(argv[1],"rb");if(!file)return 2;bytes=fread(source,1,sizeof(source),file);fclose(file);
    view.entry.language=1;view.entry.representation=DOK_ASSET_LEVEL_CP866;view.entry.bytes=(uint32_t)bytes;view.data=source;
    if(dok_level_parse(&view,&candidate_definition,&definition)!=DOK_WORLD_OK)return 3;
    if(dok_initial_world_build(&definition,1,&candidate_world,&world)!=DOK_WORLD_OK)return 4;
    file=fopen(argv[2],"rb");if(!file)return 5;bytes=fread(binding_bytes,1,sizeof(binding_bytes),file);fclose(file);
    if(bytes!=sizeof(binding_bytes))return 6;
    for(i=0;i<256u;++i)for(j=0;j<4u;++j){const unsigned char *p=binding_bytes+(i*4u+j)*8u;
        bindings[i].images[j]=(dok_geometry_image){(uint16_t)word(p),(uint16_t)word(p+2),(uint16_t)word(p+4),(uint16_t)word(p+6)};}
    dok_render_scene_init(&old_scene);dok_render_scene_init(&new_scene);
    while(scanf("%" SCNd32 " %" SCNd32 " %" SCNd32 " %" SCNu32,&camera.x,&camera.y,&camera.z,&heading)==4) {
        if(!dok_render_trig(heading,&camera.sin_q15,&camera.cos_q15))return 7;
        phase(0,1);if((result=compare(&camera,heading,1)))return result;
        phase((int16_t)(poses%30u),30);if((result=compare(&camera,heading,1)))return result;
        if(!(poses%64u)) {
            phase(0,0);if((result=compare(&camera,heading,1)))return result;
            phase(-1,30);if((result=compare(&camera,heading,1)))return result;
            phase(29,8);if((result=compare(&camera,heading,1)))return result;
            phases.revision=0;if((result=compare(&camera,heading,1)))return result;
            phase(0,1);++phases.world_epoch;if((result=compare(&camera,heading,1)))return result;
            phase(0,1);if((result=compare(&camera,heading,0)))return result;
        }
        ++poses;
    }
    camera=(dok_nds_render_camera){720,-300,-240,0,32768};
    for(i=0;i<2u;++i)for(j=0;j<2u;++j) {
        uint32_t p;fixture(i,j);
        for(p=0;p<30u;++p){phase((int16_t)p,30);if((result=compare(&camera,0,1)))return result;++focused;}
        phase(29,8);if((result=compare(&camera,0,1)))return result;++focused;
        phase(0,0);if((result=compare(&camera,0,1)))return result;++focused;
        phase(-1,30);if((result=compare(&camera,0,1)))return result;++focused;
        if(j) {
            phase(29,30);phases.phase['E']=-1;if((result=compare(&camera,0,1)))return result;++focused;
            phase(0,30);definition.objects[3].class_id=4;
            phases.phase['E']=-1;phases.extent['E']=0;if((result=compare(&camera,0,1)))return result;++focused;
        }
    }
    printf("{\"passed\":true,\"poses\":%u,\"scene_cases\":%u,\"focused_cases\":%u,"
        "\"error_returns\":%u,\"door_outputs\":%u,\"cropped_outputs\":%u,\"hidden_outputs\":%u,"
        "\"normalized_bytes_compared\":%" PRIu64 "}\n",poses,cases,focused,errors,door_outputs,cropped_outputs,hidden_outputs,bytes_compared);
    return 0;
}
