#include "dok/ai.h"
#include "dok/animation.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static dok_runtime w;static dok_runtime_level l;static dok_ai ai;static dok_combat combat;static dok_items items;
static void notice(void *p,dok_combat_kind k,uint16_t slot,uint16_t target,int16_t value){(void)p;(void)slot;(void)value;dok_ai_notice(&ai,k,target);}
static dok_runtime_storage original_storage;
static int original(unsigned lang,unsigned level){
 const char *path=lang?"originals/pkr/ENG/PKE.RES":"originals/pkr/RUS/PKR.RES";FILE *f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);uint8_t *bytes=malloc((size_t)n),*maps=malloc(3*4096),*wmaps=malloc(4*4096);dok_level *source=calloc(1,sizeof(*source));dok_level_object *defs=calloc(256,sizeof(*defs));dok_world_object *objects=calloc(256,sizeof(*objects));assert(bytes && maps && wmaps && source && defs && objects);assert(fread(bytes,1,(size_t)n,f)==(size_t)n);fclose(f);
 char name[7]="LEVEL1";name[5]=(char)('1'+level);dok_res_archive res;dok_res_entry e;dok_level_error le;dok_world_error we;dok_runtime_error re;dok_world world;
 assert(dok_res_open(&res,bytes,(size_t)n)==DOK_RES_OK);assert(dok_res_find(&res,(const uint8_t *)name,6,&e)==DOK_RES_OK);
 assert(dok_level_parse(source,bytes+e.offset,e.size,defs,256,maps,3*4096,&le)==DOK_LEVEL_OK);
 assert(dok_world_build(&world,source,&res,objects,256,wmaps,4*4096,&we)==DOK_WORLD_OK);
 assert(dok_runtime_build(&l,&original_storage,&world,&re)==DOK_RUNTIME_OK);
 free(objects);free(defs);free(source);free(wmaps);free(maps);free(bytes);return 1;
}

static const struct {unsigned level,glyph;} groups[14]={{0,0x80},{0,0x81},{0,0x8c},{0,0xa0},{0,0xa1},{1,0x80},{1,0xa1},{1,0xa0},{2,0x80},{2,0xb3},{2,0xa0},{2,0xc6},{2,0xd6},{2,0xa1}};
static unsigned reset(const dok_rom_assets *a,unsigned lang,unsigned group){
 unsigned level=groups[group].level;original(lang,level);assert(dok_runtime_reset(&w,&l)==DOK_RUNTIME_OK);
 dok_items_init(&items,&w,0,0);dok_combat_init(&combat,&w,a,&items,level,lang,0,notice,0);dok_ai_init(&ai,&w,a,level,lang,0,0,0);dok_ai_connect_combat(&ai,&combat);
 memset(w.maps,' ',l.cells*2);unsigned id=l.glyph_index[groups[group].glyph]-1;assert(id<w.object_count);
 dok_runtime_actor *m=(dok_runtime_actor *)dok_runtime_actor_at(&w,(uint16_t)id),*hero=(dok_runtime_actor *)dok_runtime_actor_at(&w,w.player_index-1);assert(m && hero);
 m->x=10*480+240;m->z=10*480+240;m->heading=0;m->base_speed=0;m->accumulator_a=m->accumulator_b=0;
 hero->x=m->x;hero->z=m->z+480;w.maps[10*l.settings[DOK_LEVEL_WIDTH]+10]=m->glyph;w.maps[11*l.settings[DOK_LEVEL_WIDTH]+10]=hero->glyph;
 return id;
}
static void poll(unsigned id){unsigned i=l.definitions[id].actor_index;dok_ai_poll(&ai,ai.owners[i].events[0].deadline);}
int main(void){
 FILE *f=fopen("build/gba/assets/assets.bin","rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);void *data=malloc((size_t)n);assert(data && fread(data,1,(size_t)n,f)==(size_t)n);fclose(f);dok_rom_assets a;assert(dok_rom_open(&a,data,(size_t)n)==DOK_ROM_OK);
 for(unsigned lang=0;lang<2;lang++)for(unsigned g=0;g<14;g++){
  unsigned id=reset(&a,lang,g),index=l.definitions[id].actor_index;int hp=w.objects[id].state.monster.health;
  /* First projectile/contact only wakes; every group then reaches a real melee cycle. */
  assert(dok_combat_damage(&combat,(uint16_t)id,w.player_index-1,1));assert(w.objects[id].state.monster.health==hp && ai.wakes==1);
  poll(id);assert(w.objects[id].state.monster.state==2 && w.objects[id].frame==0 && !ai.melee);
  uint32_t false_counter=99;assert(!dok_runtime_read_u(&w,(uint16_t)id,0x50,2,&false_counter) && false_counter==99);
  int playerhp=w.player.health;for(unsigned i=0;i<100 && !ai.melee;i++)poll(id);
  assert(ai.melee==1 && w.player.health==playerhp-5 && !ai.steps);
  assert(dok_combat_damage(&combat,(uint16_t)id,w.player_index-1,1));assert(w.objects[id].state.monster.state==3 && ai.hurts==1);
  poll(id);assert(w.objects[id].state.monster.state==2 && !ai.errors);uint16_t deathresource=0;assert(dok_rom_binding(&deathresource,&a,groups[g].level,lang,(uint16_t)id,0xec)==DOK_ROM_OK);assert(w.objects[id].visual_resource==deathresource && w.objects[id].frame==0);
  assert(dok_combat_damage(&combat,(uint16_t)id,w.player_index-1,32767));assert(!w.objects[id].state.monster.health);
  poll(id);assert(w.objects[id].state.monster.state==4 && w.objects[id].frame==0 && ai.deaths==1);
  dok_rom_resource rr;dok_animation_view av;assert(dok_rom_resource_at(&rr,&a,deathresource,lang)==DOK_ROM_OK && dok_animation_open(rr.data,rr.size,&av)==DOK_ASSET_OK);
  for(unsigned frame=1;frame<av.frame_count;frame++){poll(id);assert(!ai.completed && w.objects[id].frame==frame && ai.scheduler.owner_count==1);}
  poll(id);
  assert(ai.completed==1 && !ai.scheduler.owner_count && !ai.owners[index].alive && !ai.errors);
  assert((w.objects[id].flags&0x410)==0x10 && w.objects[id].state.monster.corpse && w.objects[id].visual_resource==deathresource);
  unsigned pos=10*l.settings[DOK_LEVEL_WIDTH]+10;assert(w.maps[pos]==' ' && w.maps[l.cells+pos]==groups[g].glyph);uint32_t height=99;assert(dok_runtime_read_u(&w,(uint16_t)id,0x19,2,&height) && !height);
  unsigned glyph=l.definitions[id].glyph;assert(l.glyph_index[glyph]==id+1);assert(!dok_combat_damage(&combat,(uint16_t)id,w.player_index-1,1));
  /* A nonempty TMap keeps its object and receives corpse in MMap. */
  id=reset(&a,lang,g);assert(dok_ai_wake(&ai,(uint16_t)id));unsigned nonempty=0;while(nonempty<l.object_count && l.definitions[nonempty].class_id!=7)++nonempty;assert(nonempty<l.object_count);w.maps[l.cells+pos]=l.definitions[nonempty].glyph;w.objects[id].state.monster.health=0;
  for(unsigned i=0;i<100 && !ai.completed;i++)poll(id);assert(ai.completed && w.maps[pos]==glyph && w.maps[l.cells+pos]==l.definitions[nonempty].glyph);
  /* Strict timer quotient and per-group projectile selection/zero-burst guard. */
  id=reset(&a,lang,g);dok_runtime_actor *hero=(dok_runtime_actor *)dok_runtime_actor_at(&w,w.player_index-1);w.maps[11*l.settings[DOK_LEVEL_WIDTH]+10]=' ';hero->z+=480;w.maps[12*l.settings[DOK_LEVEL_WIDTH]+10]=hero->glyph;
  assert(dok_ai_wake(&ai,(uint16_t)id));int32_t delay=0,limit=0,projectile=0;dok_runtime_read_s(&w,(uint16_t)id,0xa8,2,&delay);dok_runtime_read_s(&w,(uint16_t)id,0xa6,2,&limit);dok_runtime_read_s(&w,(uint16_t)id,0xe7,1,&projectile);
  dok_ai_poll(&ai,(uint32_t)(delay+1)*10000-1);assert(!ai.ranged);
  dok_ai_poll(&ai,(uint32_t)(delay+1)*10000+10000);assert(ai.ranged==(unsigned)(limit>0));
  if(limit){assert(combat.active==1);dok_combat_projectile *shot=&combat.slots[combat.cursor];assert(l.definitions[shot->object].glyph==projectile && shot->deadline==ai.now+416);
   for(int burst=1;burst<limit;burst++){poll(id);assert(w.objects[id].state.monster.shot_count==burst+1 && ai.ranged==(unsigned)(burst+1));}
   poll(id);assert(!w.objects[id].state.monster.shot_count && w.objects[id].state.monster.burst_started==ai.now && ai.ranged==(unsigned)limit);
  }
  else assert(!combat.active && !w.objects[id].state.monster.shot_count);
  dok_combat_clear(&combat);
  if(limit){
   /* A blocking TMap breaks the native 100-unit two-plane sight march. */
   unsigned wall=0;while(wall<l.object_count && (l.definitions[wall].class_id!=2 || (w.objects[wall].flags&0x18)))++wall;assert(wall<l.object_count);
   w.maps[l.cells+11*l.settings[DOK_LEVEL_WIDTH]+10]=l.definitions[wall].glyph;
   unsigned previous=ai.ranged;dok_ai_poll(&ai,ai.now+(uint32_t)(delay+1)*10000);assert(ai.ranged==previous && !w.objects[id].state.monster.shot_count && !combat.active);
  }
  assert(dok_combat_damage(&combat,(uint16_t)id,w.player_index-1,1));assert(w.objects[id].state.monster.state==3);poll(id);assert(w.objects[id].state.monster.state==1);uint16_t walking=0;assert(dok_rom_binding(&walking,&a,groups[g].level,lang,(uint16_t)id,DOK_ROM_SLOT_ANIMATION)==DOK_ROM_OK && w.objects[id].visual_resource==walking);
  dok_collision_overlay(0,0,0);
  printf("E%02u %s: wake melee hurt death corpse timer projectile passed\n",g+1,lang?"en":"ru");
 }
 /* Actual projectile poll publishes the borrowed MMap overlay. The AI must
  * see class8 at its front even while static MMap remains space. */
 for(unsigned lang=0;lang<2;lang++){
  unsigned id=reset(&a,lang,0);dok_runtime_actor *m=(dok_runtime_actor *)dok_runtime_actor_at(&w,(uint16_t)id),*hero=(dok_runtime_actor *)dok_runtime_actor_at(&w,w.player_index-1);
  memset(w.maps,' ',l.cells*2);hero->x=10*480+240;hero->z=5*480+240;hero->heading=0;m->z=30*480+240;
  assert(dok_combat_spawn(&combat,w.player_index-1,'1'));dok_combat_poll(&combat,416);dok_combat_projectile *shot=&combat.slots[combat.cursor];assert((shot->state&0x4f)==0x41);
  assert(dok_combat_object_at(&combat,shot->x,shot->z)==shot->object && dok_combat_blocked(&combat,shot->x,shot->z));
  m->x=shot->x;m->z=shot->z-480;m->heading=0;assert(dok_ai_wake(&ai,(uint16_t)id));dok_ai_poll(&ai,50000);
  assert(!ai.ranged && !w.objects[id].state.monster.shot_count && w.objects[id].state.monster.burst_started==50000);
  int32_t x=shot->x,z=shot->z;dok_combat_clear(&combat);assert(dok_combat_object_at(&combat,x,z)==DOK_RUNTIME_NONE);dok_collision_overlay(0,0,0);
  /* Dynamic MMap follows the same edge clamp as the static lookup. */
  id=reset(&a,lang,0);m=(dok_runtime_actor *)dok_runtime_actor_at(&w,(uint16_t)id);hero=(dok_runtime_actor *)dok_runtime_actor_at(&w,w.player_index-1);memset(w.maps,' ',l.cells*2);
  hero->x=10*480+240;hero->z=3240;hero->heading=180;m->z=30*480+240;w.maps[6*l.settings[DOK_LEVEL_WIDTH]+10]=hero->glyph;
  assert(dok_combat_spawn(&combat,w.player_index-1,'1'));dok_combat_poll(&combat,416);shot=&combat.slots[combat.cursor];assert((shot->state&0x4f)==0x41 && shot->z>=0 && shot->z<480);
  m->x=shot->x;m->z=0;m->heading=180;assert(dok_ai_wake(&ai,(uint16_t)id));dok_ai_poll(&ai,50000);assert(!ai.ranged && w.objects[id].state.monster.burst_started==50000);dok_combat_clear(&combat);dok_collision_overlay(0,0,0);
 }
 assert(sizeof(dok_runtime_object)==36 && sizeof(dok_runtime_player)==60);free(data);puts("G15 host: 14 groups x RU/EN passed");return 0;
}

