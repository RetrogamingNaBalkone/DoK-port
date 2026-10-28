#include "dok/ai.h"
#include "dok/combat.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static dok_runtime w;static dok_runtime_level l;static dok_ai ai;
static dok_combat combat;static dok_items items;static unsigned reclaims;
static void combat_notice(void *p,dok_combat_kind k,uint16_t slot,uint16_t target,int16_t value){(void)p;(void)slot;(void)value;if(k==DOK_COMBAT_RECLAIM){++reclaims;dok_ai_remove(&ai,target);}}
static dok_runtime_storage original_storage;
static int original(unsigned lang){
 const char *path=lang?"originals/pkr/ENG/PKE.RES":"originals/pkr/RUS/PKR.RES";FILE *f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);uint8_t *bytes=malloc((size_t)n),*maps=malloc(3*4096),*wmaps=malloc(4*4096);dok_level *source=calloc(1,sizeof(*source));dok_level_object *defs=calloc(256,sizeof(*defs));dok_world_object *objects=calloc(256,sizeof(*objects));assert(bytes && maps && wmaps && source && defs && objects);assert(fread(bytes,1,(size_t)n,f)==(size_t)n);fclose(f);
 dok_res_archive res;dok_res_entry e;dok_level_error le;dok_world_error we;dok_runtime_error re;dok_world world;
 assert(dok_res_open(&res,bytes,(size_t)n)==DOK_RES_OK);assert(dok_res_find(&res,(const uint8_t *)"LEVEL1",6,&e)==DOK_RES_OK);
 assert(dok_level_parse(source,bytes+e.offset,e.size,defs,256,maps,3*4096,&le)==DOK_LEVEL_OK);
 assert(dok_world_build(&world,source,&res,objects,256,wmaps,4*4096,&we)==DOK_WORLD_OK);
 assert(dok_runtime_build(&l,&original_storage,&world,&re)==DOK_RUNTIME_OK);
 free(objects);free(defs);free(source);free(wmaps);free(maps);free(bytes);return 1;
}
static int original_mode;
static uint32_t trace(const dok_rom_assets *a,unsigned lang){
 if(original_mode)original(lang);else assert(dok_rom_level(&l,a,0,lang)==DOK_ROM_OK);
 assert(dok_runtime_reset(&w,&l)==DOK_RUNTIME_OK);dok_ai_init(&ai,&w,a,0,lang,0,0,0);
 uint32_t hash=2166136261u;
 for(unsigned tick=416;tick<100000;tick+=416){
  for(unsigned id=0;id<w.object_count;id++)dok_ai_consider(&ai,(uint16_t)id);
  dok_ai_poll(&ai,tick);
  for(unsigned id=0;id<w.actor_count;id++){hash=(hash^(uint32_t)w.actors[id].x)*16777619u;hash=(hash^(uint32_t)w.actors[id].z)*16777619u;hash=(hash^(uint32_t)w.actors[id].heading)*16777619u;}
 }
 assert(ai.wakes && ai.steps && ai.courses && !ai.errors);
 for(unsigned id=0;id<w.object_count;id++)dok_ai_remove(&ai,(uint16_t)id);assert(!ai.scheduler.owner_count);
 return hash;
}
int main(int argc,char **argv){
 if(argc>1 && !strcmp(argv[1],"heading")){int x,z;while(scanf("%d %d",&x,&z)==2)printf("%d\n",dok_ai_heading(x,z));return 0;}
 FILE *f=fopen("build/gba/assets/assets.bin","rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);void *data=malloc((size_t)n);assert(data && fread(data,1,(size_t)n,f)==(size_t)n);fclose(f);dok_rom_assets a;assert(dok_rom_open(&a,data,(size_t)n)==DOK_ROM_OK);
 assert(dok_ai_near(3999,-4000,4001));assert(!dok_ai_near(4000,0,4000));assert(!dok_ai_near(INT32_MIN,0,4000));
 for(unsigned lang=0;lang<2;lang++){uint32_t h=trace(&a,lang);assert(trace(&a,lang)==h);printf("trace %u %08x\n",lang,h);}
 original_mode=1;uint32_t ru=trace(&a,0),en=trace(&a,1);assert(ru!=en);assert(trace(&a,0)==ru && trace(&a,1)==en);printf("original LEVEL1 trace RU %08x EN %08x\n",ru,en);
 assert(dok_runtime_reset(&w,&l)==DOK_RUNTIME_OK);dok_ai_init(&ai,&w,&a,0,1,0,0,0);
 unsigned id=0;while(id<w.object_count && (l.definitions[id].class_id!=9 || !l.definitions[id].placements))++id;assert(id<w.object_count);
 unsigned index=l.definitions[id].actor_index;int hp=w.objects[id].state.monster.health;assert(dok_ai_wake(&ai,(uint16_t)id));assert(!dok_ai_wake(&ai,(uint16_t)id));assert(w.objects[id].state.monster.health==hp && ai.owners[index].events[0].deadline==416);
 dok_ai_remove(&ai,(uint16_t)id);assert(!ai.owners[index].alive && !ai.scheduler.owner_count);dok_ai_poll(&ai,100000);assert(!ai.steps);
 w.objects[id].state.monster.state=0;assert(dok_ai_wake(&ai,(uint16_t)id));assert(ai.owners[index].count==1);
 /* Every corner: an active actor cannot cross the compact world margin.
  * The actor glyph still owns its final cell after rejected displacement. */
 for(unsigned corner=0;corner<4;corner++){
  assert(dok_runtime_reset(&w,&l)==DOK_RUNTIME_OK);dok_ai_init(&ai,&w,&a,0,1,0,0,0);
  memset(w.maps,' ',l.cells*2);dok_runtime_actor *m=&w.actors[index],*p=&w.actors[l.definitions[w.player_index-1].actor_index];
  int r=l.settings[DOK_LEVEL_MIN_DISTANCE],side=l.settings[DOK_LEVEL_CELL_SIDE];int maxx=l.settings[DOK_LEVEL_WIDTH]*side-r-1,maxz=l.settings[DOK_LEVEL_LENGTH]*side-r-1;
  m->x=corner&1?maxx:r;m->z=corner&2?maxz:r;m->base_speed=60;m->accumulator_a=m->accumulator_b=0;
  p->x=corner&1?maxx+10000:r-10000;p->z=corner&2?maxz+10000:r-10000;
  w.maps[(m->z/side)*l.settings[DOK_LEVEL_WIDTH]+m->x/side]=m->glyph;
  assert(dok_ai_wake(&ai,(uint16_t)id));for(unsigned tick=416;tick<20000;tick+=416)dok_ai_poll(&ai,tick);
  assert(!ai.errors && m->x>=r && m->x<=maxx && m->z>=r && m->z<=maxz);
  assert(w.maps[(m->z/side)*l.settings[DOK_LEVEL_WIDTH]+m->x/side]==m->glyph);
 }
 /* Front class1 belongs to G15: no motion or fabricated attack completion. */
 assert(dok_runtime_reset(&w,&l)==DOK_RUNTIME_OK);dok_ai_init(&ai,&w,&a,0,1,0,0,0);memset(w.maps,' ',l.cells*2);
 dok_runtime_actor *m=&w.actors[index],*hero=&w.actors[l.definitions[w.player_index-1].actor_index];m->x=15*480+240;m->z=15*480+240;m->heading=0;
 hero->x=m->x;hero->z=m->z+480;w.maps[15*64+15]=m->glyph;w.maps[16*64+15]=hero->glyph;
 assert(dok_ai_wake(&ai,(uint16_t)id));dok_ai_poll(&ai,416);assert(ai.contacts==1 && !ai.steps && m->x==15*480+240 && m->z==15*480+240);
 /* Actual combat replacement notification must unlink a live AI owner. */
 assert(dok_runtime_reset(&w,&l)==DOK_RUNTIME_OK);dok_ai_init(&ai,&w,&a,0,1,0,0,0);assert(dok_ai_wake(&ai,(uint16_t)id));
 unsigned glyph=l.definitions[id].glyph,space=l.glyph_index[' '];assert(space);
 for(unsigned g=0;g<glyph;g++)if(!l.glyph_index[g])l.glyph_index[g]=(uint16_t)space;
 w.objects[id].flags|=0x400;dok_items_init(&items,&w,0,0);dok_combat_init(&combat,&w,&a,&items,0,1,0,combat_notice,0);
 assert(dok_combat_spawn(&combat,w.player_index-1,'1'));assert(reclaims==1 && !ai.owners[index].alive && !ai.scheduler.owner_count);dok_combat_clear(&combat);
 dok_collision_overlay(0,0,0);free(data);puts("G14 host: deterministic RU/EN traces, threshold, wake, owner removal/reuse passed");return 0;
}
