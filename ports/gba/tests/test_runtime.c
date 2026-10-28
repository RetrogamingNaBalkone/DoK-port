#include "dok/runtime.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do {if(!(x)) {fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}} while(0)
static const char input[]=
    "[Game Ini]\nLevel Width=2\nLevel Height=1\nLevel Length=2\nCell Side=480\nCell Height=960\n"
    "Floor Color=10\nCeiling Color=143\nMin Dis=238\n[Palette]\nPalette=PAL\n[Light]\nLight=LIT\n"
    "[Game Object]\nSprite=IMG 8\nFlic=ANI\n"
    "_ 0 16 5 0 128 10 CEIL FLOOR WALL WALL\n"
    "P 1 4 4 300 7 120 90 5 100 0 100 100 3 1 1 1 1 0 0 SHOT DEAD 0 0 0\n"
    "N 9 8 7 7 12 20 0 6 ATTACK DIE SHOT DEAD 12 4000 3 4 3 10 message\n"
    "$ 3 24 3 0 6 99 20000 money\nG 7 64 5 0 14 4 1 40000\n"
    "U 5 8 5 0 14 10 0 6\nX 8 8 6 250 5 3000 0 HIT -3 MISS\n"
    "I 3 2072 4 0 6 10 1 key\nK 12 16 10 0 4 400 CEIL FLOOR WALL WALL 100 -150\n"
    "S 6 0 1 0 0\nW 10 0 1 0 0 2 5 1 weapon\n"
    "[TMap]\n$I\n$$\n[MMap]\nNP\nNN\n[FCMap]\n..\n..\n";
static dok_level level;
static dok_level_object definitions[256];
static dok_world_object objects[256],expected[256];
static uint8_t level_maps[3*64*64],world_maps[4*64*64];
static dok_runtime_storage storage;
static dok_runtime runtime,baseline;
static void word(uint8_t *p,unsigned value) {p[0]=(uint8_t)value;p[1]=(uint8_t)(value>>8);}
static int scalar(unsigned glyph,size_t off,unsigned width,int32_t expected_value) {
    int32_t n=123;return dok_runtime_read_s(&runtime,(uint16_t)(runtime.level->glyph_index[glyph]-1),off,width,&n) && n==expected_value;
}
int main(void) {
    uint8_t res[69]={0};dok_res_archive archive;dok_world world;dok_level_error le;dok_world_error we;dok_runtime_error re;
    dok_runtime_level rom,bad;size_t i,j;uint32_t a,b;int32_t x=123,y=456,z=789,n=123;uint16_t ni,ui;
    char many[8192];size_t used;unsigned glyph,count;
    word(res,1);memcpy(res+2,"ANI",3);res[18]=37;res[22]=32;
    word(res+32,1);word(res+40,65535);word(res+42,2);word(res+44,65533);
    word(res+50,1);word(res+52,1);res[60]=1;res[68]=42;
    CHECK(dok_res_open(&archive,res,sizeof(res))==DOK_RES_OK);
    CHECK(dok_level_parse(&level,input,sizeof(input)-1,definitions,256,level_maps,sizeof(level_maps),&le)==DOK_LEVEL_OK);
    CHECK(dok_world_build(&world,&level,&archive,objects,256,world_maps,sizeof(world_maps),&we)==DOK_WORLD_OK);
    CHECK(dok_runtime_build(&rom,&storage,&world,&re)==DOK_RUNTIME_OK);
    CHECK(dok_runtime_reset(&runtime,&rom)==DOK_RUNTIME_OK);
    CHECK(!memcmp(runtime.maps,"NNNP$$$I    ",12));
    CHECK(rom.object_count==11 && rom.actor_count==3 && runtime.pickup_99_total==-5536);
    CHECK(runtime.player_index==level.glyph_index['P']);
    ni=(uint16_t)(rom.glyph_index['N']-1);ui=(uint16_t)(rom.glyph_index['U']-1);
    CHECK(rom.definitions[ni].placements==3);
    CHECK(dok_runtime_actor_at(&runtime,(uint16_t)(rom.glyph_index[runtime.maps[0]]-1))==
          dok_runtime_actor_at(&runtime,(uint16_t)(rom.glyph_index[runtime.maps[2]]-1)));
    CHECK(dok_runtime_position(&runtime,ni,&x,&y,&z) && x==240 && y==-7 && z==720);
    x=123;y=456;z=789;
    CHECK(!dok_runtime_position(&runtime,ui,&x,&y,&z) && x==123 && y==456 && z==789);
    CHECK(!dok_runtime_position(&runtime,UINT16_MAX,&x,&y,&z));
    CHECK(scalar('P',0x26,4,720) && scalar('P',0x2a,4,-300) && scalar('P',0x2e,4,720));
    CHECK(runtime.player.health==100 && runtime.player.energy==0 && runtime.player.lives==3);
    CHECK(scalar('G',0x5c,2,4) && scalar('G',0x66,4,40000));
    CHECK(scalar('X',0x3c,4,-3000) && scalar('X',0x2a,4,-250) && scalar('X',0x5e,2,-2));
    CHECK(scalar('I',0x20,2,2) && scalar('I',0x22,2,-3) && scalar('I',0x52,2,-1));
    CHECK(scalar('K',0x50,2,18) && scalar('S',0x58,2,0) && scalar('W',0x83,2,4));
    CHECK(!dok_runtime_read_s(&runtime,ni,0xac,2,&n) && n==123);
    CHECK(!dok_runtime_read_s(&runtime,ni,0x44,4,&n) && n==123);
    CHECK(!dok_runtime_read_s(&runtime,ni,SIZE_MAX,4,&n) && n==123);
    CHECK(!dok_runtime_read_s(&runtime,ni,0,3,&n) && n==123);
    for(i=0;i<level.object_count;++i) for(j=0;j<DOK_WORLD_STATE_BYTES;++j) {
        int wa=dok_world_read_u(&objects[i],j,1,&a),rb=dok_runtime_read_u(&runtime,(uint16_t)i,j,1,&b);
        CHECK(wa==rb);CHECK(!wa || a==b);
    }
    memcpy(expected,objects,sizeof(objects));baseline=runtime;
    /* Mutable changes are visible through diagnostics; descriptors remain ROM. */
    runtime.player.health=-5;CHECK(scalar('P',0x5e,2,-5));
    runtime.objects[ni].state.monster.health=9;CHECK(scalar('N',0xa4,2,9));
    runtime.actors[rom.definitions[ni].actor_index].x=-123;CHECK(scalar('N',0x26,4,-123));
    memset(runtime.maps,255,sizeof(runtime.maps));runtime.objects[255].flags=123;runtime.actors[127].x=567;
    CHECK(dok_runtime_reset(&runtime,&rom)==DOK_RUNTIME_OK && !memcmp(&runtime,&baseline,sizeof(runtime)));
    /* Known null weapon IDs do not pretend to be native non-null pointers. */
    runtime.player.weapons[0]=1;n=123;
    CHECK(!dok_runtime_read_s(&runtime,(uint16_t)(rom.glyph_index['P']-1),0xaa,4,&n) && n==123);
    CHECK(dok_runtime_reset(&runtime,&rom)==DOK_RUNTIME_OK);
    bad=rom;bad.object_count=257;
    CHECK(dok_runtime_reset(&runtime,&bad)==DOK_RUNTIME_CAPACITY && !memcmp(&runtime,&baseline,sizeof(runtime)));
    bad=rom;bad.actor_count=129;CHECK(dok_runtime_reset(&runtime,&bad)==DOK_RUNTIME_CAPACITY);
    bad=rom;bad.field_count=2049;CHECK(dok_runtime_reset(&runtime,&bad)==DOK_RUNTIME_CAPACITY);
    bad=rom;bad.text_size=16385;CHECK(dok_runtime_reset(&runtime,&bad)==DOK_RUNTIME_CAPACITY);
    bad=rom;bad.cells=4097;CHECK(dok_runtime_reset(&runtime,&bad)==DOK_RUNTIME_CAPACITY);
    bad=rom;bad.glyph_index['N']=0;CHECK(dok_runtime_reset(&runtime,&bad)==DOK_RUNTIME_DEFINITION);
    bad=rom;bad.palette.offset=UINT16_MAX;CHECK(dok_runtime_reset(&runtime,&bad)==DOK_RUNTIME_DEFINITION);
    bad=rom;bad.player_index=rom.glyph_index['U'];CHECK(dok_runtime_reset(&runtime,&bad)==DOK_RUNTIME_DEFINITION);
    CHECK(dok_runtime_reset(0,&rom)==DOK_RUNTIME_ARGUMENT);
    CHECK(dok_runtime_reset(&runtime,0)==DOK_RUNTIME_ARGUMENT);
    /* No pointers to the stage6 source are retained. Destroy all source-owned
     * definitions/state/maps; independent ROM reset and scalar reads survive. */
    memset(objects,0,sizeof(objects));memset(definitions,0,sizeof(definitions));
    memset(world_maps,255,sizeof(world_maps));memset(level_maps,255,sizeof(level_maps));
    CHECK(dok_runtime_reset(&runtime,&rom)==DOK_RUNTIME_OK);
    for(i=0;i<rom.object_count;++i) for(j=0;j<DOK_WORLD_STATE_BYTES;++j) {
        int wa=dok_world_read_u(&expected[i],j,1,&a),rb=dok_runtime_read_u(&runtime,(uint16_t)i,j,1,&b);
        CHECK(wa==rb);CHECK(!wa || a==b);
    }
    dok_runtime_release(&runtime);CHECK(!runtime.level && !runtime.object_count && !runtime.maps[0]);
    CHECK(!dok_runtime_position(&runtime,ni,&x,&y,&z));dok_runtime_release(0);
    /* Capacity129 is a real set of129 actor definitions including unplaced
     * class5 templates, not an out-of-bounds fake pointer/count combination. */
    used=(size_t)snprintf(many,sizeof(many),"[Game Ini]\nLevel Width=1\nLevel Height=1\nLevel Length=1\nCell Side=480\nCell Height=960\nFloor Color=0\nCeiling Color=0\nMin Dis=1\n[Palette]\nPalette=P\n[Light]\nLight=L\n[Game Object]\n_ 2 0 1 0 0\n");
    for(glyph=33,count=0;count<129;++glyph) {
        if(glyph=='#' || glyph=='[' || glyph=='_' || glyph=='.') continue;
        CHECK(glyph<=255 && used<sizeof(many));
        used+=(size_t)snprintf(many+used,sizeof(many)-used,"%c 5 8 1 0 0 10 0 6\n",glyph);++count;
    }
    used+=(size_t)snprintf(many+used,sizeof(many)-used,"[TMap]\n.\n[MMap]\n.\n[FCMap]\n.\n");
    CHECK(used<sizeof(many));
    CHECK(dok_level_parse(&level,many,used,definitions,256,level_maps,sizeof(level_maps),&le)==DOK_LEVEL_OK);
    CHECK(dok_world_build(&world,&level,0,objects,256,world_maps,sizeof(world_maps),&we)==DOK_WORLD_OK);
    CHECK(dok_runtime_build(&rom,&storage,&world,&re)==DOK_RUNTIME_CAPACITY);
    CHECK(re.object_index==129); /* index0 is empty definition; actor129 fails */
    level.object_count=257;CHECK(dok_runtime_build(&rom,&storage,&world,&re)==DOK_RUNTIME_CAPACITY);
    CHECK(dok_runtime_build(0,&storage,&world,&re)==DOK_RUNTIME_ARGUMENT);
    puts("runtime: shared glyph/last placement, unknowns, scalar coverage, ROM ownership, reset/release and all capacity bounds passed");
    return 0;
}
