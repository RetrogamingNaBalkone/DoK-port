#include "dok/items.h"
#include <stdio.h>
static dok_runtime world;
static dok_runtime_level level;
int main(void)
{
 int category,value,h,e,hm,em,keys,score;dok_items items;
 world.level=&level;dok_items_init(&items,&world,0,0);
 while(scanf("%d%d%d%d%d%d%d%d",&category,&value,&h,&e,&hm,&em,&keys,&score)==8) {
  world.player=(dok_runtime_player){0};world.player.health=(int16_t)h;world.player.energy=(int16_t)e;
  world.player.health_max=(int16_t)hm;world.player.energy_max=(int16_t)em;
  world.player.keys=(uint16_t)keys;world.player.score=(int16_t)score;
  int accepted=dok_items_stat(&items,(int16_t)category,(int16_t)value);
  printf("%d %d %d %d %d %u %d %u\n",accepted,world.player.health,world.player.energy,
   world.player.health_max,world.player.energy_max,world.player.keys,world.player.score,world.player.map_revealed);
 }
 return 0;
}
