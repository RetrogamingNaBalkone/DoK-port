#include "dok/render_scene.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
extern void baseline_dok_render_scene_init(dok_render_scene *);
extern dok_render_scene_result baseline_dok_render_scene_prepare(dok_render_scene *,
    const dok_initial_world *,const dok_geometry_binding [256],const dok_nds_render_camera *,
    uint32_t,const dok_nds_render_frame **);
extern dok_render_scene_result baseline_dok_render_scene_release(dok_render_scene *,uint32_t);
static dok_render_scene old_scene,new_scene,old_normalized,new_normalized;
static dok_level_definition definition,candidate_definition;
static dok_initial_world world,candidate_world;
static dok_geometry_binding bindings[256];
static unsigned char source[65536],binding_bytes[8192];
static unsigned word(const unsigned char *p){return (unsigned)p[0]|(unsigned)p[1]<<8;}

static int normalize(const dok_render_scene *s,dok_render_scene *copy)
{
    uint32_t i;*copy=*s;
    for(i=0;i<DOK_RENDER_CELLS;++i)if(s->cells[i].quads) {
        if(s->cells[i].quads<s->quads || s->cells[i].quads>s->quads+DOK_RENDER_QUADS)return 0;
        /* Clear process addresses only after range validation. The main loop
         * separately compares each old/new pointer's exact quad-array offset. */
        copy->cells[i].quads=NULL;
    }
    if(s->frame.cells && s->frame.cells!=s->cells)return 0;
    copy->frame.cells=NULL;return 1;
}
int main(int argc,char **argv)
{
    FILE *file;size_t bytes;dok_asset_view view={0};uint32_t i,j,heading,cases=0,errors=0;
    uint64_t compared_bytes=0;dok_nds_render_camera camera={0};
    if(argc!=3)return 1;
    file=fopen(argv[1],"rb");if(!file)return 2;bytes=fread(source,1,sizeof(source),file);fclose(file);
    view.entry.language=1;view.entry.representation=DOK_ASSET_LEVEL_CP866;view.entry.bytes=(uint32_t)bytes;view.data=source;
    if(dok_level_parse(&view,&candidate_definition,&definition)!=DOK_WORLD_OK)return 3;
    if(dok_initial_world_build(&definition,1,&candidate_world,&world)!=DOK_WORLD_OK)return 4;
    file=fopen(argv[2],"rb");if(!file)return 5;bytes=fread(binding_bytes,1,sizeof(binding_bytes),file);fclose(file);
    if(bytes!=sizeof(binding_bytes))return 6;
    for(i=0;i<256u;++i)for(j=0;j<4u;++j) {
        const unsigned char *p=binding_bytes+(i*4u+j)*8u;
        bindings[i].images[j]=(dok_geometry_image){(uint16_t)word(p),(uint16_t)word(p+2),(uint16_t)word(p+4),(uint16_t)word(p+6)};
    }
    baseline_dok_render_scene_init(&old_scene);dok_render_scene_init(&new_scene);
    while(scanf("%" SCNd32 " %" SCNd32 " %" SCNd32 " %" SCNu32,&camera.x,&camera.y,&camera.z,&heading)==4) {
        const dok_nds_render_frame *a=NULL,*b=NULL;dok_render_scene_result sa,sb;
        if(!dok_render_trig(heading,&camera.sin_q15,&camera.cos_q15))return 7;
        sa=baseline_dok_render_scene_prepare(&old_scene,&world,bindings,&camera,heading,&a);
        sb=dok_render_scene_prepare(&new_scene,&world,bindings,&camera,heading,&b);
        if(sa!=sb || (sa==DOK_RENDER_SCENE_OK && (a!=&old_scene.frame || b!=&new_scene.frame)) ||
            (sa!=DOK_RENDER_SCENE_OK && (a || b)))return 8;
        if(!normalize(&old_scene,&old_normalized) || !normalize(&new_scene,&new_normalized))return 9;
        for(i=0;i<DOK_RENDER_CELLS;++i) {
            const dok_nds_render_quad *oldp=old_scene.cells[i].quads,*newp=new_scene.cells[i].quads;
            if((oldp==NULL)!=(newp==NULL) || (oldp && oldp-old_scene.quads!=newp-new_scene.quads))return 10;
        }
        if(memcmp(&old_normalized,&new_normalized,sizeof(old_normalized))) {
            fprintf(stderr,"Scene mismatch case %u camera %" PRId32 ",%" PRId32 ",%" PRId32 " heading %u\n",
                cases,camera.x,camera.y,camera.z,heading);return 11;
        }
        ++cases;compared_bytes+=sizeof(old_normalized);
        if(sa==DOK_RENDER_SCENE_OK) {
            if(baseline_dok_render_scene_release(&old_scene,a->generation)!=DOK_RENDER_SCENE_OK ||
                dok_render_scene_release(&new_scene,b->generation)!=DOK_RENDER_SCENE_OK)return 12;
        } else ++errors;
    }
    printf("{\"passed\":true,\"scenes\":%u,\"error_returns\":%u,\"normalized_bytes_compared\":%" PRIu64 "}\n",
        cases,errors,compared_bytes);return 0;
}
