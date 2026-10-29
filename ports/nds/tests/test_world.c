#include "dok/world.h"
#include <stdio.h>
#include <string.h>

static unsigned checks,failures;
#define CHECK(e) do { ++checks; if(!(e)) { ++failures; fprintf(stderr,"world:%d: %s\n",__LINE__,#e); } } while(0)
static dok_level_definition base,candidate,saved;
static dok_initial_world world,world_candidate,world_saved;
static const unsigned char fixture[]=
 "[Game Ini]\r\nLevel Width=2\r\nLevel Height=1\r\nLevel Length=2\r\n"
 "Cell Side=480\r\nCell Height=960\r\nFloor Color=10\r\nCeiling Color=143\r\nMin Dis=238\r\nStep Ver=4\r\n"
 "[Palette]\r\nPalette=PAL\r\n[Light]\r\nLight=LIGHT\r\n[Game Object]\r\n"
 "Sprite=IMAGE 2\r\nFlic=ANIM\r\nSound=SOUND\r\n"
 "a 0 0 7 0 257 1 I0 I1 I2 I3\r\n"
 "M 1 4 4 300 7 -2147483648 90 5 100 0 100 100 3 1 1 1 1 0 0 S0 S1 0 0 0\r\n"
 "_ 2 65535 -32768 32767 -1\r\n"
 "d 3 0 1 0 0 99 32767  notice with spaces  \r\n"
 "e 4 0 1 0 0\r\n"
 "f 5 0 1 0 0 2147483647 -32768 32767\r\n"
 "g 6 0 1 0 0\r\n"
 "h 7 0 1 0 0 -32768 32767 40000\r\n"
 "i 8 0 1 0 0 -3 1 A0 -3 A1\r\n"
 "X 9 8 7 20 12 20 0 6 A0 A1 S0 S1 12 4000 3 4 3 10  message\r\n"
 "j 10 0 1 0 0 1 2 3  text\r\n"
 "k 12 0 1 0 0 1 I0 I1 I2 I3 2 3\r\n"
 "l 13 0 1 0 0 1 I0 I1 I2 I3 Q -2147483648\r\n"
 "Sound=\r\nm 2 0 1 0 0\r\n"
 "[TMap]\r\ndd\r\ndd\r\n[MMap]\r\nMX\r\nX.\r\n[FCMap]\r\n..\r\n..\r\n";
static dok_asset_view view_of(const unsigned char *bytes,uint32_t n)
{
    dok_asset_view view;
    memset(&view,0,sizeof(view));view.data=bytes;view.entry.bytes=n;
    view.entry.representation=DOK_ASSET_LEVEL_CP866;view.entry.language=1;
    view.entry.source_index=18;view.entry.id=UINT32_C(1078478848);return view;
}
static void put16(unsigned char *p,uint16_t x)
{ p[0]=(unsigned char)x;p[1]=(unsigned char)(x>>8); }
static void put32(unsigned char *p,uint32_t x)
{ put16(p,(uint16_t)x);put16(p+2,(uint16_t)(x>>16)); }
static void parse_tests(void)
{
    dok_asset_view view=view_of(fixture,(uint32_t)sizeof(fixture)-1u);
    const dok_level_object *o;
    const dok_level_field *field;
    unsigned char changed[sizeof(fixture)];
    unsigned char line_endings[sizeof(fixture)];
    unsigned char maps[32];
    uint32_t i,n;
    CHECK(dok_level_parse(&view,&candidate,&base)==DOK_WORLD_OK);
    CHECK(base.object_count==14u && base.settings[0]==2 && base.settings[1]==1 && base.settings[2]==2);
    CHECK(base.ignored_step_ver==1u && base.truncated_messages==0u);
    CHECK(base.glyph_index[' ']==3u && base.glyph_index['_']==0u);
    CHECK(base.objects[0].marker==1u && base.objects[2].marker==255u);
    CHECK(base.objects[1].field_count==19u && base.objects[9].field_count==14u);
    CHECK(dok_level_field_at(&base.objects[1],0x38)->value==INT32_MIN);
    CHECK(dok_level_field_at(&base.objects[5],0x38)->value==INT32_MAX);
    CHECK(dok_level_field_at(&base.objects[12],0x67)->value==INT32_MIN);
    field=dok_level_field_at(&base.objects[9],0xe7);
    CHECK(field && field->kind==DOK_LEVEL_CHARACTER && field->value=='3' && field->token.length==1u);
    o=&base.objects[3];field=dok_level_field_at(o,0x58);
    CHECK(field && dok_level_span_equal(&base,field->token,(const unsigned char *)"  notice with spaces  ",22u));
    CHECK(base.objects[0].sprite_flags==2 && base.objects[0].flags==0u);
    CHECK(dok_level_span_equal(&base,base.objects[9].animation,(const unsigned char *)"ANIM",4u));
    CHECK(base.objects[13].sound.length==0u);
    CHECK(base.objects[0].raw_line.length>base.objects[0].raw_tail.length);
    CHECK(memcmp(base.maps[DOK_LEVEL_MMAP],"MXX.",4u)==0);
    saved=base;
    CHECK(dok_level_parse(&view,&base,&base)==DOK_WORLD_INVALID_ARGUMENT);
    CHECK(memcmp(&base,&saved,sizeof(base))==0);
    for(n=0;n<(uint32_t)sizeof(fixture)-1u;++n) {
        view.entry.bytes=n;
        /* Removing just the final newline leaves a complete valid file. */
        if(n>=sizeof(fixture)-3u) continue;
        CHECK(dok_level_parse(&view,&candidate,&base)!=DOK_WORLD_OK);
        CHECK(memcmp(&base,&saved,sizeof(base))==0);
    }
    view.entry.bytes=(uint32_t)sizeof(fixture)-1u;
    for(i=0,n=0;i<sizeof(fixture)-1u;++i) if(fixture[i]!='\r') line_endings[n++]=fixture[i];
    view=view_of(line_endings,n);CHECK(dok_level_parse(&view,&candidate,&base)==DOK_WORLD_OK);
    for(i=0;i<n;++i) if(line_endings[i]=='\n') line_endings[i]='\r';
    CHECK(dok_level_parse(&view,&candidate,&base)==DOK_WORLD_OK);
    memcpy(changed,fixture,sizeof(fixture));changed[0]=0;view=view_of(changed,(uint32_t)sizeof(fixture)-1u);
    saved=base;CHECK(dok_level_parse(&view,&candidate,&base)==DOK_WORLD_MALFORMED);
    CHECK(memcmp(&base,&saved,sizeof(base))==0);
    memcpy(changed,fixture,sizeof(fixture));
    { unsigned char *p=(unsigned char *)strstr((char *)changed,"2147483647");p[9]='8'; }
    CHECK(dok_level_parse(&view,&candidate,&base)==DOK_WORLD_MALFORMED);
    memcpy(changed,fixture,sizeof(fixture));
    { unsigned char *p=(unsigned char *)strstr((char *)changed,"e 4");p[0]='a'; }
    CHECK(dok_level_parse(&view,&candidate,&base)==DOK_WORLD_DUPLICATE);
    memcpy(changed,fixture,sizeof(fixture));
    { unsigned char *p=(unsigned char *)strstr((char *)changed,"e 4");p[2]='5'; }
    CHECK(dok_level_parse(&view,&candidate,&base)==DOK_WORLD_MALFORMED);
    view=view_of(fixture,(uint32_t)sizeof(fixture)-1u);CHECK(dok_level_parse(&view,&candidate,&base)==DOK_WORLD_OK);
    memset(maps,0,sizeof(maps));put16(maps,2);put16(maps+2,2);put16(maps+4,1);put16(maps+6,3);
    put32(maps+8,480);put32(maps+12,960);memcpy(maps+20,"MXX.dddd....",12u);
    view=view_of(maps,sizeof(maps));view.entry.representation=DOK_ASSET_LEVEL_MAPS;
    CHECK(dok_level_validate_maps(&base,&view)==DOK_WORLD_OK);
    maps[20]^=1u;CHECK(dok_level_validate_maps(&base,&view)==DOK_WORLD_MAP_MISMATCH);
    maps[20]^=1u;view.entry.bytes=19;CHECK(dok_level_validate_maps(&base,&view)==DOK_WORLD_MAP_MISMATCH);
}
static void world_tests(void)
{
    unsigned char source_copy[sizeof(fixture)];
    memcpy(source_copy,fixture,sizeof(fixture));saved=base;
    CHECK(dok_initial_world_build(&base,7,&world_candidate,&world)==DOK_WORLD_OK);
    CHECK(world.epoch==7u && world.object_count==14u && world.actor_placements==3u && world.placed_actors==2u);
    CHECK(world.hero.object_id==2u && world.hero.world_epoch==7u && world.hero.generation==1u);
    CHECK(world.objects[1].x==240 && world.objects[1].y==-300 && world.objects[1].z==720 && world.objects[1].heading==90);
    CHECK(world.objects[1].upper_y==-274 && world.objects[1].lower_y==-326);
    CHECK(world.objects[9].placements==2u && world.objects[9].x==720 && world.objects[9].z==720);
    CHECK(world.objects[9].y==-20 && world.objects[9].upper_y==-20 && world.objects[9].lower_y==-20);
    CHECK(world.objects[5].pose_known==0u && world.objects[5].placements==0u);
    CHECK(world.category99_total==-4);
    CHECK(memcmp(world.maps[DOK_WORLD_MMAP],"X MX",4u)==0);
    CHECK(memcmp(world.maps[DOK_WORLD_TMAP],"dddd",4u)==0);
    CHECK(memcmp(world.maps[DOK_WORLD_FCMAP],"    ",4u)==0);
    { unsigned i;for(i=0;i<DOK_LEVEL_MAX_CELLS;++i) CHECK(world.maps[DOK_WORLD_VISITED][i]==0); }
    CHECK(memcmp(&base,&saved,sizeof(base))==0 && memcmp(fixture,source_copy,sizeof(fixture))==0);
    world_saved=world;
    CHECK(dok_initial_world_build(&base,0,&world_candidate,&world)==DOK_WORLD_INVALID_ARGUMENT);
    CHECK(memcmp(&world,&world_saved,sizeof(world))==0);
    base.language=2;CHECK(dok_initial_world_build(&base,7,&world_candidate,&world)==DOK_WORLD_UNSUPPORTED);base=saved;
    base.maps[DOK_LEVEL_FCMAP][0]='?';CHECK(dok_initial_world_build(&base,7,&world_candidate,&world)==DOK_WORLD_MISSING_GLYPH);base=saved;
    base.settings[DOK_LEVEL_LENGTH]=1;CHECK(dok_initial_world_build(&base,7,&world_candidate,&world)==DOK_WORLD_UNSUPPORTED);base=saved;
    CHECK(memcmp(&world,&world_saved,sizeof(world))==0);
    base.objects[1].height_parameter=INT16_MIN;
    CHECK(dok_initial_world_build(&base,UINT32_MAX,&world_candidate,&world)==DOK_WORLD_OK);
    CHECK(world.objects[1].y==INT16_MIN && world.objects[1].upper_y==-32742 && world.objects[1].lower_y==32742);
    base=saved;
}
int main(void)
{
    parse_tests();world_tests();
    printf("N05 world: %u checks, %u failures; level=%zu seed=%zu workspace=%zu resident=%zu\n",
      checks,failures,sizeof(dok_level_definition),sizeof(dok_initial_world),sizeof(dok_world_workspace),
      2u*sizeof(dok_level_definition)+sizeof(dok_initial_world));
    return failures ? 1 : 0;
}
