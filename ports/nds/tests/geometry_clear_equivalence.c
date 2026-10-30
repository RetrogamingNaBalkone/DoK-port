#include "dok/geometry.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

extern dok_geometry_status baseline_dok_geometry_build_cell(const dok_initial_world *,
    const dok_geometry_binding [256],uint32_t,uint32_t,dok_geometry_cell *,dok_geometry_cell *);
static dok_level_definition definition,candidate_definition;
static dok_initial_world world,candidate_world;
static dok_geometry_binding bindings[256];
static unsigned char source[65536],binding_bytes[8192];
static dok_geometry_cell old_candidate,new_candidate,old_out,new_out,unchanged;
static uint32_t cases,errors;
static unsigned word(const unsigned char *p){return (unsigned)p[0]|(unsigned)p[1]<<8;}

static int compare(uint32_t x,uint32_t z,uint32_t pattern)
{
    dok_geometry_status old_status,new_status;
    memset(&old_candidate,(int)(pattern&255u),sizeof(old_candidate));
    memcpy(&new_candidate,&old_candidate,sizeof(new_candidate));
    memset(&old_out,(int)((pattern^255u)&255u),sizeof(old_out));
    memcpy(&new_out,&old_out,sizeof(new_out));unchanged=old_out;
    old_status=baseline_dok_geometry_build_cell(&world,bindings,x,z,&old_candidate,&old_out);
    new_status=dok_geometry_build_cell(&world,bindings,x,z,&new_candidate,&new_out);
    if(old_status!=new_status || memcmp(&old_candidate,&new_candidate,sizeof(new_candidate)) ||
        memcmp(&old_out,&new_out,sizeof(new_out)) ||
        (new_status!=DOK_GEOMETRY_OK && memcmp(&new_out,&unchanged,sizeof(new_out)))) {
        fprintf(stderr,"Geometry clear mismatch case %u cell %u,%u pattern %u status %u/%u\n",
            cases,x,z,pattern,(unsigned)old_status,(unsigned)new_status);return 20;
    }
    ++cases;if(new_status!=DOK_GEOMETRY_OK)++errors;
    return 0;
}

int main(int argc,char **argv)
{
    FILE *file;size_t bytes;dok_asset_view view={0};uint32_t x,z,i,j,k,width,length;
    uint32_t base_cases=0,mutated=0;int result;
    if(argc!=3)return 1;
    file=fopen(argv[1],"rb");if(!file)return 2;bytes=fread(source,1,sizeof(source),file);fclose(file);
    view.entry.language=1;view.entry.representation=DOK_ASSET_LEVEL_CP866;
    view.entry.bytes=(uint32_t)bytes;view.data=source;
    if(dok_level_parse(&view,&candidate_definition,&definition)!=DOK_WORLD_OK)return 3;
    if(dok_initial_world_build(&definition,1,&candidate_world,&world)!=DOK_WORLD_OK)return 4;
    file=fopen(argv[2],"rb");if(!file)return 5;bytes=fread(binding_bytes,1,sizeof(binding_bytes),file);fclose(file);
    if(bytes!=sizeof(binding_bytes))return 6;
    for(i=0;i<256u;++i)for(j=0;j<4u;++j) {
        const unsigned char *p=binding_bytes+(i*4u+j)*8u;
        bindings[i].images[j]=(dok_geometry_image){(uint16_t)word(p),(uint16_t)word(p+2),
            (uint16_t)word(p+4),(uint16_t)word(p+6)};
    }
    width=(uint32_t)definition.settings[DOK_LEVEL_WIDTH];length=(uint32_t)definition.settings[DOK_LEVEL_LENGTH];
    for(z=0;z<64u;++z)for(x=0;x<64u;++x) {
        for(i=0;i<3u;++i) {
            if((result=compare(x,z,i==0u?0xa5u:i==1u?0x5au:0xc3u)))return result;
            ++base_cases;
        }
        if(x>=width || z>=length)continue;
        for(i=0;i<2u;++i) {
            uint32_t glyph=world.maps[i?DOK_WORLD_FCMAP:DOK_WORLD_TMAP][z*width+x];
            uint32_t index=definition.glyph_index[glyph];
            if(!index || index>definition.object_count)return 7;
            glyph=definition.objects[index-1u].glyph;
            for(j=0;j<4u;++j) {
                dok_geometry_image saved=bindings[glyph].images[j];
                for(k=0;k<5u;++k) {
                    bindings[glyph].images[j]=saved;
                    if(k==0u)bindings[glyph].images[j].material=0;
                    else if(k==1u)bindings[glyph].images[j].width=0;
                    else if(k==2u)bindings[glyph].images[j].height=0;
                    else if(k==3u)bindings[glyph].images[j].width=32768;
                    else bindings[glyph].images[j].height=32768;
                    if((result=compare(x,z,(x*37u+z*13u+j*7u+k)&255u)))return result;
                    ++mutated;
                }
                bindings[glyph].images[j]=saved;
            }
        }
    }
    printf("{\"passed\":true,\"cells\":4096,\"base_sentinel_cases\":%u,"
        "\"mutated_binding_cases\":%u,\"cases\":%u,\"error_returns\":%u,"
        "\"candidate_bytes\":%u,\"out_bytes\":%u}\n",base_cases,mutated,cases,errors,
        (unsigned)sizeof(new_candidate),(unsigned)sizeof(new_out));
    return 0;
}
