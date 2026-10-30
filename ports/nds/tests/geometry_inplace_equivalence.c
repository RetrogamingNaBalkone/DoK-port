#define main public_equivalence_main
#include "geometry_clear_equivalence.c"
#undef main
#include "../render/geometry_private.h"
static dok_geometry_cell inplace;
static uint32_t private_cases,private_errors,tail_bytes_checked,argument_cases;
static int compare_private(uint32_t x,uint32_t z,uint32_t poison)
{
    dok_geometry_status a,b;uint32_t i;unsigned char *tail;
    memset(&old_candidate,(int)poison,sizeof(old_candidate));
    memset(&old_out,(int)(poison^255u),sizeof(old_out));memset(&inplace,(int)poison,sizeof(inplace));
    a=baseline_dok_geometry_build_cell(&world,bindings,x,z,&old_candidate,&old_out);
    b=dok_geometry_build_cell_inplace(&world,bindings,x,z,&inplace);
    if(a!=b)return 30;
    if(b==DOK_GEOMETRY_OK) {
        if(inplace.count!=old_out.count || inplace.reserved!=old_out.reserved ||
            memcmp(inplace.quads,old_out.quads,(size_t)inplace.count*sizeof(inplace.quads[0])))return 31;
        tail=(unsigned char *)(void *)(inplace.quads+inplace.count);
        for(i=0;i<(DOK_GEOMETRY_CELL_MAX-inplace.count)*sizeof(inplace.quads[0]);++i) {
            if(tail[i]!=(unsigned char)poison)return 32;++tail_bytes_checked;
        }
    } else ++private_errors;
    ++private_cases;return 0;
}
static int arguments(void)
{
    uint32_t mode;const dok_level_definition *saved=world.definition;
    for(mode=0;mode<10u;++mode) {
        dok_geometry_status a,b;const dok_initial_world *w=mode==0u?NULL:&world;
        const dok_geometry_binding *images=mode==1u?NULL:bindings;uint32_t x=0,z=0;
        dok_geometry_cell *ca=&old_candidate,*cb=&new_candidate,*oa=&old_out,*ob=&new_out;
        memset(ca,0xa5,sizeof(*ca));memset(cb,0xa5,sizeof(*cb));memset(oa,0x5a,sizeof(*oa));memset(ob,0x5a,sizeof(*ob));
        if(mode==2u){ca=NULL;cb=NULL;}if(mode==3u){oa=NULL;ob=NULL;}
        if(mode==4u){oa=ca;ob=cb;}if(mode==5u)world.definition=NULL;
        if(mode==6u)x=64u;if(mode==7u)z=64u;if(mode==8u)x=UINT32_MAX;if(mode==9u)z=UINT32_MAX;
        a=baseline_dok_geometry_build_cell(w,images,x,z,ca,oa);b=dok_geometry_build_cell(w,images,x,z,cb,ob);
        world.definition=saved;
        if(a!=b || memcmp(&old_candidate,&new_candidate,sizeof(old_candidate)) || memcmp(&old_out,&new_out,sizeof(old_out)))return 33;
        ++argument_cases;
    }
    return 0;
}
int main(int argc,char **argv)
{
    uint32_t x,z,i,j,k,width,length;int result=public_equivalence_main(argc,argv);
    static const unsigned char poisons[]={0,255,165,90,195,105,128,1};
    if(result)return result;if((result=arguments()))return result;
    width=(uint32_t)definition.settings[DOK_LEVEL_WIDTH];length=(uint32_t)definition.settings[DOK_LEVEL_LENGTH];
    for(z=0;z<64u;++z)for(x=0;x<64u;++x) {
        for(i=0;i<sizeof(poisons);++i)if((result=compare_private(x,z,poisons[i])))return result;
        if(x>=width || z>=length)continue;
        for(i=0;i<2u;++i) {
            uint32_t glyph=world.maps[i?DOK_WORLD_FCMAP:DOK_WORLD_TMAP][z*width+x];
            glyph=definition.objects[definition.glyph_index[glyph]-1u].glyph;
            for(j=0;j<4u;++j) {
                dok_geometry_image saved=bindings[glyph].images[j];
                for(k=0;k<5u;++k) {
                    bindings[glyph].images[j]=saved;
                    if(k==0u)bindings[glyph].images[j].material=0;
                    else if(k==1u)bindings[glyph].images[j].width=0;
                    else if(k==2u)bindings[glyph].images[j].height=0;
                    else if(k==3u)bindings[glyph].images[j].width=32768;
                    else bindings[glyph].images[j].height=32768;
                    if((result=compare_private(x,z,poisons[(x+z+j+k)&7u])))return result;
                }
                bindings[glyph].images[j]=saved;
            }
        }
    }
    printf("{\"passed\":true,\"public_cases\":%u,\"public_error_returns\":%u,"
        "\"public_argument_cases\":%u,\"private_cases\":%u,\"private_error_returns\":%u,"
        "\"poison_patterns\":8,\"inactive_tail_bytes_checked\":%u}\n",cases,errors,argument_cases,private_cases,private_errors,tail_bytes_checked);
    return 0;
}
