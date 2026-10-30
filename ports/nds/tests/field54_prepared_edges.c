#define main fixture_main
#include "geometry_clear_equivalence.c"
#undef main
extern const dok_level_field *counted_prepared_field_at(const dok_level_object *,uint16_t);
#define dok_level_field_at counted_prepared_field_at
#include "../render/geometry_private.h"
#undef dok_level_field_at
extern const dok_level_field *dok_level_field_at(const dok_level_object *,uint16_t);
static uint32_t prepare_calls;
const dok_level_field *counted_prepared_field_at(const dok_level_object *o,uint16_t offset)
{++prepare_calls;return dok_level_field_at(o,offset);}
static dok_level_definition saved_definition;
static dok_geometry_field54 prepared[DOK_LEVEL_MAX_OBJECTS];
static dok_geometry_cell active;
int main(int argc,char **argv)
{
    uint32_t mode,i,x,z,checks=0;int result=fixture_main(argc,argv);
    if(result)return result;saved_definition=definition;
    for(mode=0;mode<10u;++mode) {
        unsigned char untouched[sizeof(prepared)];definition=saved_definition;
        if(mode==0u)definition.language=0;
        if(mode==1u)definition.object_count=0;
        if(mode==2u)definition.object_count=DOK_LEVEL_MAX_OBJECTS+1u;
        if(mode==3u)definition.settings[DOK_LEVEL_WIDTH]=0;
        if(mode==4u)definition.settings[DOK_LEVEL_LENGTH]=0;
        if(mode==5u)definition.settings[DOK_LEVEL_WIDTH]=65;
        if(mode==6u)definition.settings[DOK_LEVEL_LENGTH]=65;
        if(mode==7u)definition.settings[DOK_LEVEL_CELL_SIDE]=0;
        if(mode==8u)definition.settings[DOK_LEVEL_CELL_HEIGHT]=0;
        if(mode==9u)definition.settings[DOK_LEVEL_CELL_HEIGHT]=-1;
        memset(prepared,0xa5,sizeof(prepared));memcpy(untouched,prepared,sizeof(prepared));prepare_calls=0;
        dok_geometry_prepare_fields(&definition,prepared);
        if(prepare_calls || memcmp(untouched,prepared,sizeof(prepared)))return 42;
    }
    for(mode=0;mode<12u;++mode) {
        definition=saved_definition;
        for(i=0;i<definition.object_count;++i) {
            dok_level_object *o=&definition.objects[i];
            /* Deliberately duplicate stored glyphs independently of the lookup
             * table; metadata must continue to follow actual object slots. */
            if(mode==0u)o->glyph=definition.objects[0].glyph;
            if(mode==1u)definition.glyph_index[o->glyph]=(uint16_t)(1u+(i+1u)%definition.object_count);
            if(mode>=2u) {
                o->field_count=2;
                o->fields[0].native_offset=0x54;o->fields[0].kind=DOK_LEVEL_WORD;
                o->fields[0].value=(int32_t)((i*997u+mode*32767u)&65535u);
                o->fields[1]=o->fields[0];o->fields[1].value=1234;
                if(mode==3u)o->fields[0].kind=DOK_LEVEL_LONG;
                if(mode==4u)o->fields[0].native_offset=0x56;
                if(mode==5u)o->fields[0].native_offset=o->fields[1].native_offset=0x56;
                if(mode==6u)o->field_count=DOK_LEVEL_MAX_FIELDS+1;
                if(mode==7u)o->field_count=0;
                if(mode==8u)o->fields[0].value=INT32_MIN;
                if(mode==9u)o->fields[0].value=INT32_MAX;
                if(mode==10u)o->fields[0].value=32768;
                if(mode==11u)o->fields[0].value=-1;
            }
        }
        memset(prepared,0xa5,sizeof(prepared));dok_geometry_prepare_fields(&definition,prepared);
        for(i=0;i<definition.object_count;++i) {
            const dok_level_field *f=dok_level_field_at(&definition.objects[i],0x54);
            uint32_t v=f?(uint32_t)f->value&65535u:0u;
            int32_t signed_v=v<32768u?(int32_t)v:(int32_t)v-65536;
            if(prepared[i].value!=signed_v || prepared[i].is_word!=(uint16_t)(f && f->kind==DOK_LEVEL_WORD))return 40;
        }
        for(z=0;z<64u;++z)for(x=0;x<64u;++x) {
            dok_geometry_status a,b;memset(&old_candidate,0xa5,sizeof(old_candidate));memset(&old_out,0x5a,sizeof(old_out));
            memset(&active,0xc3,sizeof(active));
            a=baseline_dok_geometry_build_cell(&world,bindings,x,z,&old_candidate,&old_out);
            b=dok_geometry_build_cell_prepared(&world,bindings,x,z,&active,prepared);
            if(a!=b || (a==DOK_GEOMETRY_OK && (active.count!=old_out.count || active.reserved!=old_out.reserved ||
                memcmp(active.quads,old_out.quads,active.count*sizeof(active.quads[0])))))return 41;
            if((result=compare(x,z,mode+19u)))return result;++checks;
        }
    }
    printf("{\"passed\":true,\"mutation_modes\":12,\"prepared_cells\":%u,\"public_cells\":%u}\n",checks,cases);return 0;
}
