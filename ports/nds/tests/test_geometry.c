#include "dok/geometry.h"
#include <stdio.h>
#include <string.h>
static dok_level_definition level;
static dok_initial_world world;
static dok_geometry_binding bindings[256];
static dok_geometry_cell candidate,out,sentinel;
static unsigned checks;
#define CHECK(condition) do {checks++;if(!(condition)){fprintf(stderr,"geometry:%d\n",__LINE__);return 1;}}while(0)
static void setup(void)
{
    uint32_t i,j;
    memset(&level,0,sizeof(level)); memset(&world,0,sizeof(world));
    memset(bindings,0,sizeof(bindings));
    level.language=1u; level.object_count=5u;
    level.settings[0]=3; level.settings[1]=1; level.settings[2]=3;
    level.settings[3]=480; level.settings[4]=960;
    for(i=0u;i<5u;i++) {
        static const uint16_t classes[5]={0u,2u,4u,7u,0u};
        dok_level_object *o=&level.objects[i];
        o->glyph=(uint8_t)(i+1u); o->class_id=classes[i]; o->width_parameter=5;
        o->flags=(uint16_t)(i+100u); level.glyph_index[i+1u]=(uint16_t)(i+1u);
        if(i==0u || i==4u) {
            o->field_count=1u; o->fields[0].native_offset=0x54u;
            o->fields[0].value=i==0u?10:400;
        }
        for(j=0u;j<4u;j++) {
            dok_geometry_image *im=&bindings[i+1u].images[j];
            im->material=(uint16_t)(i*4u+j+1u); im->width=96u;
            im->height=192u; im->flags=(uint16_t)(0x100u+i);
        }
    }
    memset(world.maps,1,sizeof(world.maps)); world.definition=&level;
}
int main(void)
{
    uint32_t pattern,kind,side;
    static const uint8_t positions[4]={1u,3u,7u,5u};
    static const uint8_t glyphs[4]={1u,2u,3u,4u};
    setup();
    for(kind=3u;kind<=4u;kind++) for(pattern=0u;pattern<256u;pattern++) {
        uint32_t n=pattern,blocked[4],expected,doors=0u;
        world.maps[DOK_WORLD_TMAP][4]=(unsigned char)kind;
        for(side=0u;side<4u;side++) {
            uint32_t index=n%4u; n/=4u;
            world.maps[DOK_WORLD_TMAP][positions[side]]=glyphs[index];
            blocked[side]=index!=0u;
        }
        CHECK(dok_geometry_build_cell(&world,bindings,1u,1u,&candidate,&out)==DOK_GEOMETRY_OK);
        expected=2u+(!blocked[0]&&!blocked[2])+(!blocked[1]&&!blocked[3]);
        CHECK(out.count==expected);
        for(side=0u;side<out.count;side++) {
            const dok_geometry_quad *q=&out.quads[side];
            CHECK(q->cell_x==1u && q->cell_z==1u);
            if(q->kind==DOK_GEOMETRY_DOOR) {
                uint32_t mirror=world.maps[DOK_WORLD_TMAP][q->orientation==0u?3u:1u]==2u;
                CHECK(q->source_glyph==kind && q->object_flags==level.objects[kind-1u].flags);
                CHECK(q->vertices[0].u_q16==(mirror!=0u?95*65536:0));
                CHECK(q->image_flags==bindings[kind].images[0].flags);
                doors++;
            }
        }
        CHECK(doors==expected-2u);
    }
    setup(); world.maps[DOK_WORLD_TMAP][4]=2u; world.maps[DOK_WORLD_FCMAP][1]=5u;
    CHECK(dok_geometry_build_cell(&world,bindings,1u,1u,&candidate,&out)==DOK_GEOMETRY_OK);
    CHECK(out.count==4u && out.quads[0].kind==DOK_GEOMETRY_WALL);
    CHECK(out.quads[0].vertices[0].y==-561 && out.quads[0].vertices[0].v_q16==80*65536);
    CHECK(out.quads[0].vertices[3].y==0 && out.quads[0].vertices[3].v_q16==191*65536);
    level.objects[1].width_parameter=32767;
    CHECK(dok_geometry_build_cell(&world,bindings,1u,1u,&candidate,&out)==DOK_GEOMETRY_OK);
    CHECK(out.quads[0].vertices[1].x==384); /* low16(96*32767)=-96 */
    setup(); level.objects[0].fields[0].value=-1; level.objects[0].height_parameter=-32768;
    CHECK(dok_geometry_build_cell(&world,bindings,1u,1u,&candidate,&out)==DOK_GEOMETRY_OK);
    CHECK(out.count==1u && out.quads[0].kind==DOK_GEOMETRY_FLOOR_QUAD);
    CHECK(out.quads[0].vertices[0].y==-32768);
    memset(&out,0xa5,sizeof(out)); sentinel=out;
    bindings[1].images[DOK_GEOMETRY_FLOOR].material=0u;
    CHECK(dok_geometry_build_cell(&world,bindings,1u,1u,&candidate,&out)==DOK_GEOMETRY_MISSING_BINDING);
    CHECK(memcmp(&out,&sentinel,sizeof(out))==0);
    CHECK(dok_geometry_build_cell(&world,bindings,3u,1u,&candidate,&out)==DOK_GEOMETRY_INVALID_ARGUMENT);
    CHECK(memcmp(&out,&sentinel,sizeof(out))==0);
    CHECK(dok_geometry_build_cell(&world,bindings,1u,1u,&out,&out)==DOK_GEOMETRY_INVALID_ARGUMENT);
    CHECK(memcmp(&out,&sentinel,sizeof(out))==0);
    printf("N07 geometry: %u checks passed\n",checks);
    return 0;
}
