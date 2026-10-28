#include "dok/items.h"
#include <assert.h>
#include <stdio.h>
static dok_runtime w;
static dok_runtime_definition defs[4];
static dok_runtime_field fields[3];
static dok_runtime_level level;
static unsigned notices;
static void notice(void *p,dok_item_kind k,uint16_t id,int16_t c,int16_t v)
{(void)p;(void)k;(void)id;(void)c;(void)v;++notices;}
int main(void)
{
 dok_items s;w.level=&level;level.definitions=defs;level.fields=fields;level.cells=4;
 level.settings[DOK_LEVEL_WIDTH]=2;level.settings[DOK_LEVEL_LENGTH]=2;level.settings[DOK_LEVEL_CELL_SIDE]=480;
 level.glyph_index['@']=2;level.glyph_index['W']=3;defs[0].actor_index=0;defs[1].class_id=3;
 defs[1].field_count=2;defs[2].class_id=10;defs[2].field_count=3;
 fields[0].native_offset=0x54;fields[1].native_offset=0x56;fields[2].native_offset=0x81;
 fields[1].value.number=10;w.player_index=1;w.actor_count=1;w.object_count=3;
 w.actors[0].x=240;w.actors[0].z=240;
 dok_items_init(&s,&w,notice,0);w.player.health=w.player.health_max=100;
 w.maps[4]=w.maps[5]='@';assert(!dok_items_pickup(&s));assert(!notices);
 w.player.health=95;assert(dok_items_pickup(&s));assert(w.player.health==100);
 assert(w.maps[4]==' ' && w.maps[5]=='@');assert(!dok_items_pickup(&s));
 fields[1].value.number=-10;w.maps[4]='@';w.player.health=0;
 assert(dok_items_pickup(&s));assert(w.player.health==0 && w.maps[4]==' ');
 assert(dok_items_stat(&s,3,50));assert(w.player.energy==50 && w.player.energy_max==50);
 assert(dok_items_stat(&s,3,-60));assert(w.player.energy==-10 && w.player.energy_max==-10);
 w.player.health=32760;w.player.health_max=32767;
 assert(dok_items_stat(&s,0,20));assert(w.player.health==0);
 assert(dok_items_stat(&s,10,4));assert(dok_items_stat(&s,10,4));assert(w.player.keys==4);
 assert(dok_items_stat(&s,200,-1));assert(w.player.map_revealed);
 fields[0].value.number=3;fields[1].value.number=50;fields[2].value.number=1;
 w.maps[4]='W';assert(dok_items_pickup(&s));assert(w.player.weapons[1]==3 && w.player.selected_weapon==1);
 w.maps[4]='W';assert(dok_items_pickup(&s));assert(w.player.weapons[1]==3 && w.player.energy_max==90);
 assert(!dok_items_stat(&s,32767,1));
 puts("Items: full/refusal, signed16 overflow, max/refill, damage at0, repeated key/weapon, shared glyph, map, notifications passed");return 0;
}
