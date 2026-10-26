#include "dok/flow.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static dok_runtime w;
static dok_runtime_level level;
static dok_runtime_actor initial;
static dok_runtime_definition def;
int main(void){
 assert(!dok_flow_retry(0));assert(!dok_flow_retry(&w));
 level.settings[DOK_LEVEL_CELL_SIDE]=480;level.settings[DOK_LEVEL_WIDTH]=64;level.settings[DOK_LEVEL_LENGTH]=64;
 level.actor_count=1;level.object_count=1;level.player_index=1;level.cells=4096;level.definitions=&def;level.initial_actors=&initial;
 initial.x=480;initial.y=-300;initial.z=480;initial.heading=90;initial.angle_34=33;initial.mode=2;initial.base_speed=47;initial.step_distance=68;
 w.level=&level;w.player_index=1;w.actor_count=1;w.object_count=1;
 memset(w.maps,' ',sizeof(w.maps));w.maps[130]='@';w.maps[65]='X';w.maps[4096+65]='D';w.maps[8192+65]='F';w.maps[12288+65]=99;
 memset(&w.actors[0],0x43,sizeof(w.actors[0]));w.actors[0].x=960;w.actors[0].z=960;w.actors[0].glyph='@';
 dok_runtime_actor expected=w.actors[0];expected.x=initial.x;expected.y=initial.y;expected.z=initial.z;expected.heading=initial.heading;expected.angle_34=initial.angle_34;expected.mode=initial.mode;expected.base_speed=initial.base_speed;expected.step_distance=initial.step_distance;
 memset(&w.player,0x37,sizeof(w.player));w.player.health=0;w.player.health_max=123;dok_runtime_player player=w.player;player.health=123;
 w.objects[0].flags=0x1234;assert(dok_flow_retry(&w));
 assert(!memcmp(&w.actors[0],&expected,sizeof(expected)));assert(!memcmp(&w.player,&player,sizeof(player)));
 assert(w.maps[130]==' ' && w.maps[65]=='@' && w.maps[4096+65]=='D' && w.maps[8192+65]=='F' && w.maps[12288+65]==99 && w.objects[0].flags==0x1234);
 w.actors[0].x=-480;w.actors[0].z=999999;w.maps[4032]='!';assert(dok_flow_retry(&w));assert(w.maps[4032]==' ');
 puts("G18 flow: native pose subset, health max, retained inventory/world and occupancy clamp passed");return 0;
}
