#include "dok/world.h"
#include "dok/native_state.h"
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
    "[TMap]\n$I\n$$\n[MMap]\nNP\nNN\n[FCMap]\n..\n..\n";
static dok_level level;
static dok_level_object definitions[256];
static dok_world_object objects[256];
static uint8_t level_maps[12],world_maps[16];
static void word(uint8_t *p,unsigned value) {p[0]=(uint8_t)value;p[1]=(uint8_t)(value>>8);}
static dok_world_object *object(unsigned glyph) {return &objects[level.glyph_index[glyph]-1];}
static int value(unsigned glyph,size_t off,unsigned width,int32_t expected) {
    int32_t n=123;return dok_world_read_s(object(glyph),off,width,&n) && n==expected;
}
int main(void) {
    uint8_t res[69]={0};dok_res_archive archive;dok_world world;dok_level_error le;dok_world_error we;
    int32_t n;uint32_t u;size_t i;dok_player_state player;
    /* One ANI frame: 1x1 image, interval -1, parameters 2 and -3. */
    word(res,1);memcpy(res+2,"ANI",3);res[18]=37;res[22]=32;
    word(res+32,1);word(res+40,65535);word(res+42,2);word(res+44,65533);
    word(res+50,1);word(res+52,1);res[60]=1;res[68]=42;
    CHECK(dok_res_open(&archive,res,sizeof(res))==DOK_RES_OK);
    CHECK(dok_level_parse(&level,input,sizeof(input)-1,definitions,256,level_maps,12,&le)==DOK_LEVEL_OK);
    CHECK(dok_world_build(&world,&level,&archive,objects,256,world_maps,16,&we)==DOK_WORLD_OK);
    CHECK(!memcmp(world_maps,"NNNP$$$I    ",12));
    CHECK(!memcmp(level_maps,"$I$$NPNN....",12)); /* Input unchanged, distinct plane order. */
    for(i=12;i<16;++i) CHECK(world_maps[i]==0);
    CHECK(world.pickup_99_total==-5536 && world.player_index==level.glyph_index['P']);
    CHECK(object('N')->placements==3 && object('P')->placements==1);
    CHECK(value('N',0x26,4,240) && value('N',0x2e,4,720) && value('N',0x2a,4,-7));
    CHECK(value('N',0x9e,2,-7) && value('N',0xa0,2,-7));
    CHECK(value('P',0x26,4,720) && value('P',0x2e,4,720) && value('P',0x2a,4,-300));
    CHECK(value('P',0x9e,2,-274) && value('P',0xa0,2,-326));
    CHECK(value('P',0x36,2,3) && value('P',0x38,4,120) && value('P',0x5a,2,4));
    CHECK(value('P',0xd6,2,3) && value('P',0xaa,4,0) && value('P',0xd8,2,0));
    CHECK(dok_native_read_player(object('P')->bytes,object('P')->native_size,&player));
    CHECK(player.x==720 && player.z==720 && player.health==100 && player.lives==3);
    CHECK(value('G',0x5c,2,4) && value('G',0x66,4,40000) && value('G',0x6a,2,64));
    CHECK(value('X',0x3c,4,-3000) && value('X',0x5e,2,-2) && value('X',0x2a,4,-250));
    CHECK(value('X',0x54,2,2) && value('X',0x56,2,-3) && value('X',0x52,2,-1));
    CHECK(value('I',0x20,2,2) && value('I',0x22,2,-3) && value('I',0x50,2,0));
    CHECK(value('K',0x50,2,18) && value('K',0x52,2,0));
    n=123;u=123;
    CHECK(!dok_world_read_s(object('U'),0x26,4,&n) && n==123);
    CHECK(!dok_world_read_u(object('P'),0x10,2,&u) && u==123); /* vtable unknown */
    CHECK(!dok_world_read_u(object('P'),0x44,4,&u) && u==123); /* sprite far unknown */
    CHECK(!dok_world_read_u(object('P'),SIZE_MAX,4,&u));
    CHECK(!dok_world_read_u(object('P'),0,3,&u));
    CHECK(!dok_world_read_s(0,0,1,&n));
    CHECK(dok_world_build(&world,&level,&archive,objects,8,world_maps,16,&we)==DOK_WORLD_CAPACITY);
    CHECK(dok_world_build(&world,&level,&archive,objects,256,world_maps,15,&we)==DOK_WORLD_CAPACITY);
    CHECK(dok_world_build(&world,&level,0,objects,256,world_maps,16,&we)==DOK_WORLD_RESOURCE);
    level_maps[8]='?';
    CHECK(dok_world_build(&world,&level,&archive,objects,256,world_maps,16,&we)==DOK_WORLD_UNDEFINED_GLYPH);
    CHECK(we.plane==DOK_WORLD_FCMAP && we.glyph=='?');level_maps[8]='.';
    level.settings[DOK_LEVEL_LENGTH]=1;
    CHECK(dok_world_build(&world,&level,&archive,objects,256,world_maps,16,&we)==DOK_WORLD_PROFILE);
    level.settings[DOK_LEVEL_LENGTH]=2;level.settings[DOK_LEVEL_CELL_SIDE]=0;
    CHECK(dok_world_build(&world,&level,&archive,objects,256,world_maps,16,&we)==DOK_WORLD_PROFILE);
    level.settings[DOK_LEVEL_CELL_SIDE]=480;
    definitions[level.glyph_index['N']-1].height_parameter=INT16_MIN;
    CHECK(dok_world_build(&world,&level,&archive,objects,256,world_maps,16,&we)==DOK_WORLD_OK);
    CHECK(value('N',0x2a,4,INT16_MIN)); /* Native NEG AX wraps. */
    CHECK(dok_world_build(0,&level,&archive,objects,256,world_maps,16,&we)==DOK_WORLD_ARGUMENT);
    puts("world: map orientation, shared glyphs, startup state, unknown masks, resource frames and bounds passed");
    return 0;
}
