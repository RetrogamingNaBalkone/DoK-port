#include "dok/combat.h"
#include "dok/collision.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The driver links the pinned pre-optimization combat module under these
 * names. Compare public simulation state and ordered events, not just hits. */
void reference_combat_init(dok_combat *,dok_runtime *,const dok_rom_assets *,dok_items *,unsigned,unsigned,uint32_t,dok_combat_sink,void *);
int reference_combat_spawn(dok_combat *,uint16_t,uint8_t);
void reference_combat_poll(dok_combat *,uint32_t);
static dok_runtime worlds[2];
static dok_combat combats[2];
static dok_items inventories[2];
static uint32_t event_hash[2],event_count[2],seed=0x50455246;
static unsigned random_word(void){seed=seed*1664525u+1013904223u;return seed;}
static void notice(void *context,dok_combat_kind kind,uint16_t slot,uint16_t target,int16_t value){
 unsigned n=(unsigned)(uintptr_t)context;
 uint32_t v[]={kind,slot,target,(uint16_t)value};
 for(unsigned i=0;i<4;i++)event_hash[n]=(event_hash[n]^v[i])*16777619u;
 ++event_count[n];
}
static void equal(void){
 assert(!memcmp(&worlds[0],&worlds[1],sizeof(worlds[0])));
 dok_combat copy=combats[1];copy.world=combats[0].world;copy.items=combats[0].items;copy.context=combats[0].context;
 assert(!memcmp(&combats[0],&copy,sizeof(copy)));
 assert(event_hash[0]==event_hash[1] && event_count[0]==event_count[1]);
}
int main(int argc,char **argv){
 assert(argc==2 || (argc==3 && !strcmp(argv[2],"enemy-only")));
 unsigned enemy_only=argc==3;
 FILE *file=fopen(argv[1],"rb");assert(file);
 assert(!fseek(file,0,SEEK_END));long size=ftell(file);rewind(file);
 void *data=malloc((size_t)size);assert(data && fread(data,1,(size_t)size,file)==(size_t)size);fclose(file);
 dok_rom_assets assets;assert(dok_rom_open(&assets,data,(size_t)size)==DOK_ROM_OK);
 unsigned checks=0,cases=0;
 for(unsigned language=0;language<2;language++)for(unsigned level=0;level<3;level++){
  dok_runtime_level view;assert(dok_rom_level(&view,&assets,level,language)==DOK_ROM_OK);
  for(unsigned scenario=0;scenario<80;scenario++){
   /* Exercise fallback cell arithmetic as well as real 480-unit maps. */
   view.settings[DOK_LEVEL_CELL_SIDE]=scenario%4?480:511;
   unsigned side=view.settings[DOK_LEVEL_CELL_SIDE];
   for(unsigned n=0;n<2;n++){
    assert(dok_runtime_reset(&worlds[n],&view)==DOK_RUNTIME_OK);
    dok_items_init(&inventories[n],&worlds[n],0,0);
    event_hash[n]=2166136261u;event_count[n]=0;
   }
   dok_combat_init(&combats[0],&worlds[0],&assets,&inventories[0],level,language,0,notice,0);
   reference_combat_init(&combats[1],&worlds[1],&assets,&inventories[1],level,language,0,notice,(void *)(uintptr_t)1);
   for(unsigned id=0;id<view.object_count;id++){
    unsigned index=view.definitions[id].actor_index;if(index>=DOK_RUNTIME_MAX_ACTORS)continue;
    int32_t x=(int32_t)(random_word()%(side*12))+side*26;
    int32_t z=(int32_t)(random_word()%(side*12))+side*26;
    /* Exact edges, outside maps and overlapping targets. */
    if(scenario%8==0){x=(int32_t)((random_word()%5)*side);z=side*31;}
    if(scenario%13==0)x=-1;
    if(scenario%17==0)z=(int32_t)(side*64);
    for(unsigned n=0;n<2;n++){
     worlds[n].actors[index].x=x;worlds[n].actors[index].z=z;
     worlds[n].actors[index].heading=(int16_t)(id*37%360);
     if(view.definitions[id].class_id==9){
      worlds[n].objects[id].state.monster.state=1;
      if(id%11==0)worlds[n].objects[id].state.monster.health=0;
      if(id%17==0)worlds[n].objects[id].flags|=0x400;
     }
    }
   }
   if(scenario%2)for(unsigned n=0;n<2;n++)memset(worlds[n].maps,' ',view.cells*2);
   unsigned player=worlds[0].player_index-1;
   for(unsigned shot=0;shot<24;shot++){
    unsigned owner=player;
    if(shot%2){owner=1+random_word()%(view.object_count-1);while(!dok_runtime_actor_at(&worlds[0],(uint16_t)owner))owner=(owner+1)%view.object_count;}
    if(enemy_only && view.definitions[owner].class_id==1){
     owner=0;while(view.definitions[owner].class_id!=9 || !dok_runtime_actor_at(&worlds[0],(uint16_t)owner))++owner;
    }
    uint8_t glyph=(uint8_t)(shot%3?'1':'2');
    assert(dok_combat_spawn(&combats[0],(uint16_t)owner,glyph)==reference_combat_spawn(&combats[1],(uint16_t)owner,glyph));
   }
   equal();uint32_t tick=0;
   for(unsigned step=0;step<32;step++){
    tick+=step%7?416:10000;
    dok_combat_poll(&combats[0],tick);reference_combat_poll(&combats[1],tick);
    equal();++checks;
   }
   ++cases;
  }
 }
 printf("Battle differential: %u scenarios, %u polls, state and ordered events exact (%s)\n",cases,checks,enemy_only?"non-player owners":"mixed owners");
 free(data);return 0;
}
