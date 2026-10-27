#include "dok/rom_assets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)) {fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}} while(0)
_Alignas(4) static uint8_t blob[4096],bad[4096];
static dok_runtime runtime,snapshot;
static uint32_t used;
static void word(uint8_t *p,uint16_t n) {p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
static void dword(uint8_t *p,uint32_t n) {word(p,(uint16_t)n);word(p+2,(uint16_t)(n>>16));}
static uint32_t load32(const uint8_t *p) {return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static uint32_t block(size_t n) {uint32_t offset=(used+3)&~3u;used=offset+(uint32_t)n;return offset;}
static void fixture(void) {
    uint32_t payload,glyphs,defs,fields,ru,en,maps,objects,actors,player,bindings;unsigned level,language;
    used=64+32+6*80;memset(blob,0,sizeof(blob));
    payload=block(1);blob[payload]=42;
    glyphs=block(512);word(blob+glyphs+32*2,1);
    defs=block(32);word(blob+defs,3);word(blob+defs+6,1);blob[defs+10]=32;
    word(blob+defs+12,65535);word(blob+defs+16,3);word(blob+defs+20,0);word(blob+defs+22,1);
    fields=block(24);word(blob+fields,0x54);dword(blob+fields+4,10);
    word(blob+fields+8,0x56);dword(blob+fields+12,1);
    word(blob+fields+16,0x58);blob[fields+18]=DOK_LEVEL_TEXT;word(blob+fields+20,1);word(blob+fields+22,3);
    ru=block(4);memcpy(blob+ru,"A ru",4);en=block(4);memcpy(blob+en,"A en",4);
    maps=block(3);memset(blob+maps,32,3);
    objects=block(36);actors=block(4);player=block(60);
    bindings=block(24);word(blob+bindings,DOK_ROM_SLOT_SPRITE); /* overwritten object/slot below */
    word(blob+bindings,0);word(blob+bindings+2,DOK_ROM_SLOT_SPRITE);word(blob+bindings+4,1);blob[bindings+6]=DOK_LEVEL_IMAGE;
    word(blob+bindings+8,65535);word(blob+bindings+10,DOK_ROM_SLOT_PALETTE);word(blob+bindings+12,1);blob[bindings+14]=255;
    word(blob+bindings+16,65535);word(blob+bindings+18,DOK_ROM_SLOT_LIGHT);word(blob+bindings+20,1);blob[bindings+22]=255;
    memcpy(blob,"DOKGBA1",8);word(blob+8,1);word(blob+10,64);dword(blob+12,used);
    word(blob+16,3);word(blob+18,2);dword(blob+20,1);dword(blob+24,64);dword(blob+28,96);
    word(blob+32,80);word(blob+34,32);dword(blob+36,1);dword(blob+40,1);
    blob[64]='A';dword(blob+80,payload);dword(blob+84,1);dword(blob+88,payload);dword(blob+92,1);
    for(level=0;level<3;++level) for(language=0;language<2;++language) {
        uint8_t *v=blob+96+(level*2+language)*80;
        word(v,1);word(v+2,1);word(v+4,1);word(v+6,480);word(v+8,960);
        word(v+16,1);word(v+20,3);word(v+22,4);word(v+24,1);
        dword(v+32,glyphs);dword(v+36,defs);dword(v+40,fields);dword(v+44,language?en:ru);
        dword(v+48,maps);dword(v+52,objects);dword(v+56,actors);dword(v+60,player);
        word(v+66,1);word(v+70,1);dword(v+72,bindings);word(v+76,3);
    }
}
static int actual_package(const char *path) {
    FILE *file=fopen(path,"rb");long size;uint8_t *data;dok_rom_assets a;dok_runtime_level view;unsigned i,j;
    CHECK(file);CHECK(!fseek(file,0,SEEK_END));size=ftell(file);CHECK(size>=0 && !fseek(file,0,SEEK_SET));
    data=malloc((size_t)size);CHECK(data);CHECK(fread(data,1,(size_t)size,file)==(size_t)size);fclose(file);
    CHECK(dok_rom_open(&a,data,(size_t)size)==DOK_ROM_OK);
    for(i=0;i<3;++i) for(j=0;j<2;++j) {
        const uint8_t *text;uint16_t id;dok_rom_resource resource;
        CHECK(dok_rom_level(&view,&a,i,j)==DOK_ROM_OK);
        CHECK(dok_runtime_reset(&runtime,&view)==DOK_RUNTIME_OK);
        CHECK(dok_rom_binding(&id,&a,i,j,DOK_ROM_GLOBAL_OBJECT,DOK_ROM_SLOT_PALETTE)==DOK_ROM_OK);
        CHECK(dok_rom_resource_at(&resource,&a,id,j)==DOK_ROM_OK && resource.size==768);
        runtime.player.health=-7;runtime.player.weapons[0]=3;runtime.maps[0]^=1;
        runtime.objects[255].flags=123;runtime.actors[127].x=567;snapshot=runtime;text=view.text;
        CHECK(dok_rom_switch_language(&runtime,&view,&a,1-j)==DOK_ROM_OK);
        CHECK(!memcmp(&snapshot,&runtime,sizeof(runtime)) && text!=view.text);
        CHECK(dok_rom_switch_language(&runtime,&view,&a,j)==DOK_ROM_OK);
        CHECK(!memcmp(&snapshot,&runtime,sizeof(runtime)) && text==view.text);
    }
    dok_runtime_release(&runtime);free(data);return 0;
}
int main(int argc,char **argv) {
    dok_rom_assets a,old;dok_runtime_level view;dok_rom_resource resource;uint16_t id=123;uint32_t n,offset;dok_rom_status status;
    fixture();CHECK(dok_rom_open(&a,blob,used)==DOK_ROM_OK);old=a;
    /* Frontend boot validates only the catalog. A corrupt unchosen level
     * must still fail when selected, and full open must keep rejecting it. */
    dok_rom_assets catalog;dok_runtime_level pair[2];
    CHECK(dok_rom_open_catalog(&catalog,blob,used)==DOK_ROM_OK);
    CHECK(dok_rom_level_pair(pair,&catalog,0)==DOK_ROM_OK);
    CHECK(pair[0].maps==pair[1].maps);
    memcpy(bad,blob,used);offset=load32(blob+96+4*80+36);word(bad+offset+12,0);
    CHECK(dok_rom_open_catalog(&catalog,bad,used)==DOK_ROM_OK);
    CHECK(dok_rom_level_pair(pair,&catalog,2)!=DOK_ROM_OK);
    CHECK(dok_rom_open(&catalog,bad,used)!=DOK_ROM_OK);
    CHECK(dok_rom_level(&view,&a,0,0)==DOK_ROM_OK && view.object_count==1 && view.actor_count==0);
    CHECK(dok_runtime_reset(&runtime,&view)==DOK_RUNTIME_OK);
    CHECK(dok_rom_find(&id,&a,(const uint8_t *)"A",1)==DOK_ROM_OK && id==1);
    CHECK(dok_rom_resource_at(&resource,&a,id,0)==DOK_ROM_OK && resource.size==1 && resource.data[0]==42);
    CHECK(dok_rom_binding(&id,&a,0,0,0,DOK_ROM_SLOT_SPRITE)==DOK_ROM_OK && id==1);
    CHECK(dok_runtime_read_u(&runtime,0,0x59,1,&n) && n=='r');
    runtime.player.health=-4;runtime.maps[0]=255;runtime.objects[255].flags=123;snapshot=runtime;
    CHECK(dok_rom_switch_language(&runtime,&view,&a,1)==DOK_ROM_OK);
    CHECK(!memcmp(&snapshot,&runtime,sizeof(runtime)));
    CHECK(dok_runtime_read_u(&runtime,0,0x59,1,&n) && n=='e');
    CHECK(dok_rom_switch_language(&runtime,&view,&a,0)==DOK_ROM_OK);
    CHECK(dok_runtime_read_u(&runtime,0,0x59,1,&n) && n=='r');
    CHECK(dok_rom_switch_language(&runtime,&view,&a,2)==DOK_ROM_LANGUAGE_ERROR);
    CHECK(dok_rom_resource_at(&resource,&a,0,0)==DOK_ROM_NOT_FOUND);
    CHECK(dok_rom_resource_at(&resource,&a,1,2)==DOK_ROM_LANGUAGE_ERROR);
    CHECK(dok_rom_level(&view,&a,3,0)==DOK_ROM_NOT_FOUND);
    CHECK(dok_rom_level(&view,&a,0,2)==DOK_ROM_LANGUAGE_ERROR);
    id=123;CHECK(dok_rom_find(&id,&a,(const uint8_t *)"B",1)==DOK_ROM_NOT_FOUND && id==123);
    CHECK(dok_rom_open(&a,blob+1,used-1)==DOK_ROM_ALIGNMENT);
    CHECK(dok_rom_open(&a,blob,63)==DOK_ROM_RANGE && !memcmp(&a,&old,sizeof(a)));
#define BAD() memcpy(bad,blob,used)
#define REJECT(expected) do {status=dok_rom_open(&a,bad,used);CHECK(status==expected);CHECK(!memcmp(&a,&old,sizeof(a)));} while(0)
    BAD();word(bad+8,2);REJECT(DOK_ROM_VERSION_ERROR);
    BAD();dword(bad+12,used+1);REJECT(DOK_ROM_RANGE);
    BAD();bad[44]=1;REJECT(DOK_ROM_FORMAT);
    BAD();dword(bad+80,UINT32_MAX-3);REJECT(DOK_ROM_RANGE);
    BAD();dword(bad+80,load32(blob+80)+1);REJECT(DOK_ROM_ALIGNMENT);
    BAD();offset=load32(blob+96+36);word(bad+offset+12,0);REJECT(DOK_ROM_FORMAT);
    BAD();offset=load32(blob+96+40);word(bad+offset+20,65535);REJECT(DOK_ROM_FORMAT);
    BAD();offset=load32(blob+96+72);word(bad+offset+4,2);REJECT(DOK_ROM_REFERENCE);
    BAD();offset=load32(blob+96+72);word(bad+offset+2,0x44);REJECT(DOK_ROM_REFERENCE);
    BAD();offset=load32(blob+96+72);bad[offset+6]=DOK_LEVEL_SOUND;REJECT(DOK_ROM_REFERENCE);
    BAD();offset=load32(blob+96+72);word(bad+offset+8,0);word(bad+offset+10,DOK_ROM_SLOT_SPRITE);REJECT(DOK_ROM_REFERENCE);
    BAD();word(bad+96+76,2);REJECT(DOK_ROM_REFERENCE);
    /* Both language views must share common data/geometry, even if duplicated
     * replacement records happen to have the same values. */
    BAD();offset=load32(blob+96+48);dword(bad+96+80+48,load32(blob+96+56));
    bad[load32(blob+96+56)]=32;bad[load32(blob+96+56)+1]=32;bad[load32(blob+96+56)+2]=32;
    REJECT(DOK_ROM_PROFILE);
    (void)offset;
    CHECK(dok_rom_open(&a,0,used)==DOK_ROM_ARGUMENT);
    if(argc==2) CHECK(!actual_package(argv[1]));
    else CHECK(argc==1);
    puts("rom assets: explicit LE views, stable IDs/bindings, corruption rejection and state-preserving language switch passed");
    return 0;
}
