#include "dok/level.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char fixture[]=
    "# synthetic\n[Game Ini]\nLevel Width=1\nLevel Height=1\nLevel Length=1\n"
    "Cell Side=480\nCell Height=960\nFloor Color=10\nCeiling Color=143\nMin Dis=238\nStep Ver=4\n"
    "[Palette]\nPalette=PAL\n[Light]\nLight=LIT\n[Game Object]\nSprite=IMG 264\nFlic=ANI\nSound=SND\n"
    "_ 0 16 5 0 128 -1 CEIL FLOOR WALL WALL\n"
    "P 1 4 4 300 7 120 90 5 100 0 100 100 3 1 1 1 1 0 0 SHOT DEAD 0 0 0\n"
    "B 2 2 5 0 14\nC 3 24 3 0 6 10 -2 0123456789012345678901234567890123456789012345\n"
    "D 4 8 5 0 14\nE 5 8 5 0 14 -2147483648 -90 3\nF 6 24 3 200 128\n"
    "G 7 64 5 0 14 4 1 40000\nH 8 8 6 250 5 3000 0 HIT 14 MISS\n"
    "I 9 8 7 0 12 20 0 6 ATTACK DIE SHOT DEAD 12 4000 3 4 3 10 message\n"
    "J 10 24 4 0 1 2 3 50 weapon\nK 12 16 10 0 4 400 CEIL FLOOR WALL WALL 100 -150\n"
    "L 13 16 10 0 128 10 CEIL FLOOR WALL WALL G 2048\n"
    "[TMap]\n \n[MMap]\nP\n[FCMap]\n \n";
static dok_level level;
static dok_level_object objects[256];
static uint8_t maps[3*64*64];
static dok_level_error error;
static dok_level_status parse(const char *s,size_t n,size_t cap,size_t mapcap) {
    return dok_level_parse(&level,s,n,objects,cap,maps,mapcap,&error);
}
static void invalid_replace(const char *from,const char *to,dok_level_status expected) {
    char input[4096]; const char *p=strstr(fixture,from); size_t prefix;
    assert(p);prefix=(size_t)(p-fixture);
    memcpy(input,fixture,prefix);strcpy(input+prefix,to);strcpy(input+prefix+strlen(to),p+strlen(from));
    assert(parse(input,strlen(input),256,sizeof(maps))==expected);
    assert(error.status==expected && error.line>0);
}
int main(void) {
    size_t i,j; char cr[4096],crlf[4096];
    assert(parse(fixture,sizeof(fixture)-1,256,sizeof(maps))==DOK_LEVEL_OK);
    assert(level.object_count==13 && level.ignored_step_ver==1 && level.truncated_messages==1);
    assert(maps[0]==' ' && maps[1]=='P' && maps[2]==' ');
    assert(objects[0].glyph==' ' && objects[0].marker==128 && objects[0].fields[0].value.number==-1);
    assert(objects[1].field_count==19 && objects[1].fields[0].native_offset==0x38);
    assert(objects[5].fields[0].value.number==INT32_MIN);
    assert(objects[7].fields[2].value.number==40000);
    assert(objects[9].fields[9].kind==DOK_LEVEL_CHAR && objects[9].fields[9].value.number=='3');
    assert(objects[12].fields[5].value.number=='G');
    assert(objects[3].fields[2].value.bytes.size==39);
    assert(level.glyph_index['P']==2 && level.glyph_index[' ']==1 && !level.glyph_index['?']);
    assert(objects[0].sprite_flags==264 && objects[12].sprite.offset==objects[0].sprite.offset);
    for(i=0,j=0;i<sizeof(fixture)-1;++i) {
        cr[i]=fixture[i]=='\n'?'\r':fixture[i];
        if(fixture[i]=='\n') crlf[j++]='\r';
        crlf[j++]=fixture[i];
    }
    assert(parse(cr,sizeof(fixture)-1,256,sizeof(maps))==DOK_LEVEL_OK);
    assert(parse(crlf,j,256,sizeof(maps))==DOK_LEVEL_OK);
    assert(parse(fixture,sizeof(fixture)-1,12,sizeof(maps))==DOK_LEVEL_CAPACITY);
    assert(parse(fixture,sizeof(fixture)-1,256,2)==DOK_LEVEL_CAPACITY);
    for(i=0;i<sizeof(fixture)-2;++i) assert(parse(fixture,i,256,sizeof(maps))!=DOK_LEVEL_OK);
    invalid_replace("B 2", "P 2",DOK_LEVEL_DUPLICATE);
    invalid_replace("L 13", "L 11",DOK_LEVEL_RANGE);
    invalid_replace("Level Width=1", "Level Width=65",DOK_LEVEL_RANGE);
    invalid_replace("Level Height=1", "Level Height=2",DOK_LEVEL_RANGE);
    invalid_replace("Level Length=1", "Level Length=0",DOK_LEVEL_RANGE);
    invalid_replace("-2147483648", "2147483648",DOK_LEVEL_SYNTAX);
    invalid_replace("40000", "2147483648",DOK_LEVEL_SYNTAX);
    invalid_replace("[TMap]", "[TMap",DOK_LEVEL_SYNTAX);
    invalid_replace("[FCMap]\n ", "[FCMap]\n  ",DOK_LEVEL_SYNTAX);
    invalid_replace("Cell Side=480", "Unexpected=480",DOK_LEVEL_SYNTAX);
    invalid_replace("B 2 2 5 0 14", "B 2 2 5 0 14 extra",DOK_LEVEL_SYNTAX);
    invalid_replace("Floor Color=10", "Floor Color=-1",DOK_LEVEL_RANGE);
    invalid_replace("[TMap]", "[Unknown]",DOK_LEVEL_SYNTAX);
    invalid_replace("[TMap]\n ", "[Game Ini]\nLevel Width=2\n[TMap]\n ",DOK_LEVEL_SYNTAX);
    invalid_replace("[FCMap]\n ", "[Game Ini]\nLevel Width=2\n[FCMap]\n  ",DOK_LEVEL_RANGE);
    memcpy(cr,fixture,sizeof(fixture));cr[20]=0;
    assert(parse(cr,sizeof(fixture)-1,256,sizeof(maps))==DOK_LEVEL_SYNTAX);
    assert(dok_level_parse(0,fixture,sizeof(fixture)-1,objects,256,maps,sizeof(maps),&error)==DOK_LEVEL_ARGUMENT);
    assert(error.line==0);
    puts("LEVEL tests passed (grammar, native widths, byte fields, inheritance, bounds, truncation).");
    return 0;
}
