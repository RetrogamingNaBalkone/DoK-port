#include "dok/combat.h"
#include "dok/collision.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static dok_runtime world;
static dok_runtime_level level;
static dok_combat combat;
static dok_items items;
static unsigned notices[16];
static unsigned spawn_on_remove,spawned_slot;
static void notice(void *p,dok_combat_kind k,uint16_t s,uint16_t t,int16_t n) {
 (void)p;(void)t;(void)n;++notices[k];
 if(k==DOK_COMBAT_REMOVED && spawn_on_remove) {
  spawn_on_remove=0;assert(dok_combat_spawn(&combat,world.player_index-1,'1'));
  spawned_slot=0;while(spawned_slot<256 && !(combat.slots[spawned_slot].state&0x80))++spawned_slot;
  assert(spawned_slot<256 && spawned_slot!=s);
 }
}
static void reset(const dok_rom_assets *a) {
 assert(dok_rom_level(&level,a,0,0)==DOK_ROM_OK);
 assert(dok_runtime_reset(&world,&level)==DOK_RUNTIME_OK);
 dok_items_init(&items,&world,0,0);dok_combat_init(&combat,&world,a,&items,0,0,0,notice,0);
 world.player.weapons[1]=level.glyph_index['W'];world.player.weapons[2]=level.glyph_index['V'];
 world.player.selected_weapon=world.player.pending_weapon=1;world.player.energy=world.player.energy_max=1000;
 combat.displayed_weapon=world.player.weapons[1];
}
int main(int argc,char **argv) {
 const char *path=argc>1?argv[1]:"build/gba/assets/assets.bin";FILE *f=fopen(path,"rb");assert(f);
 fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);void *data=malloc((size_t)n);assert(data);assert(fread(data,1,(size_t)n,f)==(size_t)n);fclose(f);
 dok_rom_assets a;assert(dok_rom_open(&a,data,(size_t)n)==DOK_ROM_OK);reset(&a);
 if(argc>2) {
  for(unsigned state=0;state<10;state++) {reset(&a);combat.weapon_state=(uint16_t)state;
   int accepted=dok_combat_fire(&combat);printf("%u %d %u\n",state,accepted,combat.weapon_state);}
  free(data);return 0;
 }
 for(unsigned slot=1;slot<=2;slot++) {
  reset(&a);world.player.selected_weapon=world.player.pending_weapon=(int16_t)slot;
  world.player.energy=0;assert(!dok_combat_fire(&combat) && !combat.active);
  world.player.energy=1;assert(dok_combat_fire(&combat));assert(!world.player.energy && combat.active==1);
  uint16_t id=combat.cursor;assert(combat.slots[id].damage==(slot==1?14:4));assert(!level.glyph_index[id]);
  assert(combat.slots[id].deadline==416);dok_combat_poll(&combat,415);assert(combat.slots[id].deadline==416);
  for(unsigned tick=416;tick<100000;tick+=416)dok_combat_poll(&combat,tick);
  assert(!combat.active && combat.removed==1 && !combat.errors);
 }
 reset(&a);combat.displayed_weapon=0;dok_combat_poll(&combat,1);assert(combat.weapon_state==1 && combat.weapon_shift==2);
 dok_combat_poll(&combat,1101);dok_combat_poll(&combat,2201);dok_combat_poll(&combat,3301);assert(combat.weapon_state==7);
 reset(&a);assert(!dok_combat_request(&combat,8));assert(dok_combat_request(&combat,2));
 dok_combat_poll(&combat,1100);assert(combat.weapon_state==0 && combat.weapon_shift==1);
 dok_combat_poll(&combat,2200);dok_combat_poll(&combat,3300);assert(combat.weapon_state==4);
 dok_combat_poll(&combat,4400);assert(world.player.selected_weapon==2 && combat.weapon_state==1);
 dok_combat_poll(&combat,5500);dok_combat_poll(&combat,6600);assert(combat.weapon_state==5);
 dok_combat_poll(&combat,7700);assert(combat.weapon_state==7);assert(dok_combat_fire(&combat));
 reset(&a);unsigned free_slots=0;for(unsigned i=0;i<256;i++)free_slots+=!level.glyph_index[i];
 for(unsigned i=0;i<free_slots;i++)assert(dok_combat_spawn(&combat,world.player_index-1,'1')==14);
 assert(combat.active==free_slots);int energy=world.player.energy;
 assert(!dok_combat_fire(&combat));assert(world.player.energy==energy && combat.active==free_slots && combat.weapon_state==2);
 dok_combat_clear(&combat);assert(!combat.active && combat.cursor==DOK_RUNTIME_NONE);
 reset(&a);assert(dok_combat_spawn(&combat,world.player_index-1,'1'));
 unsigned pos=0;while(pos<level.cells && (world.maps[pos]!=' ' || world.maps[level.cells+pos]!=' '))++pos;assert(pos<level.cells);
 dok_combat_projectile *p=&combat.slots[combat.cursor];p->x=(int32_t)(pos%64)*480+240;p->z=(int32_t)(pos/64)*480+240;
 assert(!dok_combat_blocked(&combat,p->x,p->z));
 combat.occupied[pos>>3]|=(uint8_t)(1u<<(pos&7));p->state|=0x40;
 dok_collision_overlay(&world,dok_combat_blocked,&combat);assert(!dok_collision_clear(&world,p->x,p->z,0));
 int32_t x=p->x,z=p->z;dok_combat_remove(&combat,combat.cursor);assert(!dok_combat_blocked(&combat,x,z) && dok_collision_clear(&world,x,z,0));
 dok_collision_overlay(0,0,0);
 assert(dok_combat_spawn(&combat,world.player_index-1,'1')==14);
 unsigned first=combat.cursor;dok_combat_remove(&combat,(uint16_t)first);assert(dok_combat_spawn(&combat,world.player_index-1,'1')==14 && combat.cursor==first);
 reset(&a);unsigned monster=0;for(unsigned i=0;i<world.object_count;i++)if(level.definitions[i].class_id==9){monster=i;break;}
 assert(monster);int health=world.objects[monster].state.monster.health;
 world.objects[monster].state.monster.state=0;
 assert(dok_combat_damage(&combat,(uint16_t)monster,world.player_index-1,14));assert(world.objects[monster].state.monster.health==health && notices[DOK_COMBAT_WAKE]);
 world.objects[monster].state.monster.state=1;
 assert(dok_combat_damage(&combat,(uint16_t)monster,world.player_index-1,32767));assert(!world.objects[monster].state.monster.health && notices[DOK_COMBAT_DEATH]);
 assert(!dok_combat_damage(&combat,(uint16_t)monster,(uint16_t)monster,14));
 unsigned player=world.player_index-1;assert(dok_combat_damage(&combat,(uint16_t)player,(uint16_t)monster,14));assert(world.player.health==world.player.health_max-14);
 /* Runtime object marked reclaimable must win over a later NULL glyph. */
 reset(&a);unsigned glyph=' ',token=level.glyph_index[glyph];assert(token);world.objects[token-1].flags|=0x400;
 unsigned before=notices[DOK_COMBAT_RECLAIM];
 for(unsigned i=0;i<=glyph;i++) {
  assert(dok_combat_spawn(&combat,world.player_index-1,'1'));
  if(combat.cursor==glyph)break;
 }
 assert(combat.cursor==glyph && notices[DOK_COMBAT_RECLAIM]==before+1);
 /* Original mutable-count sweep visits newest first, then skips the final
  * survivor after deletions. A callback-created owner is published afterward,
  * and the currently executing deleted slot cannot be reused in its callback. */
 reset(&a);unsigned ids[3];
 for(unsigned i=0;i<3;i++) {assert(dok_combat_spawn(&combat,world.player_index-1,'1'));ids[i]=combat.cursor;
  combat.slots[ids[i]].state=3;combat.slots[ids[i]].deadline=100;}
 spawn_on_remove=1;dok_combat_poll(&combat,100);
 assert(!combat.slots[ids[2]].state && !combat.slots[ids[0]].state && combat.slots[ids[1]].state==3);
 assert(combat.active==2 && combat.cursor==spawned_slot && combat.slots[spawned_slot].state==1);
 assert(combat.slots[spawned_slot].deadline==516 && combat.slots[spawned_slot].frame==0);
 dok_combat_clear(&combat);assert(!combat.active && combat.cursor==DOK_RUNTIME_NONE);
 puts("G13 host: readiness, charge/refusal, hide/show, full/reuse, lifecycle, wake/damage/death passed");free(data);return 0;
}
