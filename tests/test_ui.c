#include "dok/ui.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static dok_runtime world;
static uint8_t framebuffer[38402],font8[2048],font16[4096];
static void release(dok_ui *u){dok_ui_input(u,0);dok_ui_game_keys(u,0);}
int main(void){
 dok_ui u;dok_ui_init(&u);assert(u.state==DOK_UI_SPLASH);
 dok_ui_input(&u,1);assert(u.state==DOK_UI_TITLE && u.action==0);release(&u);
 assert(dok_ui_input(&u,4)==1 && u.language==1);release(&u);
 assert(!dok_ui_input(&u,1) && u.state==DOK_UI_LEVELS);release(&u);
 dok_ui_input(&u,128);release(&u);assert(u.focus==1);
 dok_ui_input(&u,2);release(&u);assert(u.state==DOK_UI_TITLE && u.focus==0);
 dok_ui_input(&u,1);release(&u);dok_ui_input(&u,1);assert(u.action==DOK_UI_NEW_GAME && u.start_level==0);
 u.state=DOK_UI_PLAY;assert(dok_ui_game_keys(&u,1)==0);release(&u);
 assert(dok_ui_game_keys(&u,1)==1);assert(!dok_ui_input(&u,4) && u.language==1 && u.state==DOK_UI_MAP);release(&u);
 dok_ui_input(&u,4);assert(u.state==DOK_UI_PLAY && !dok_ui_game_keys(&u,4));release(&u);
 assert(!dok_ui_input(&u,12) && u.state==2 && u.language==1);assert(!dok_ui_game_keys(&u,12));release(&u);
 dok_ui_input(&u,128);release(&u);assert(u.focus==1);assert(dok_ui_input(&u,1)==1 && u.language==0);release(&u);
 dok_ui_input(&u,128);release(&u);dok_ui_input(&u,1);release(&u);assert(u.state==3);
 dok_ui_input(&u,2);release(&u);assert(u.state==2 && u.focus==2);
 dok_ui_input(&u,128);release(&u);dok_ui_input(&u,1);release(&u);assert(u.state==1);
 dok_ui_input(&u,8);assert(u.state==DOK_UI_LEVELS && !dok_ui_game_keys(&u,8));release(&u);
 dok_ui_init(&u);u.state=DOK_UI_TITLE;dok_ui_input(&u,64);assert(u.focus==3);release(&u);
 dok_ui_input(&u,128);assert(u.focus==0);release(&u);
 uint8_t long_text[1024];for(unsigned i=0;i<sizeof(long_text);i++)long_text[i]=(uint8_t)i;
 dok_ui_literal(&u,long_text,sizeof(long_text),0xfffff000u);assert(u.literal_size==240);
 dok_ui_tick(&u,0,0xfffff000u);assert(u.length==240 && u.pages==4);
 uint8_t out[60];assert(dok_ui_text(&u,0,out)==60);assert(out[0]==' ' && out[32]==32);
 for(unsigned i=0;i<4;i++)dok_ui_tick(&u,0,0xfffff000u+(i+1)*30000u);
 assert(!u.active);
 dok_runtime_field field={0xbe,DOK_LEVEL_TEXT,0,{.bytes={0,120}}};
 dok_runtime_definition def={0};def.field_count=1;
 dok_runtime_level level={0};level.object_count=1;level.definitions=&def;level.fields=&field;level.text=long_text;level.text_size=120;world.level=&level;
 dok_ui_notify(&u,0,0xbe,0);dok_ui_tick(&u,&level,0);assert(u.length==120 && u.pages==2);
 for(unsigned i=0;i<9;i++)dok_ui_notify(&u,0,(uint16_t)(0xbe + i),0);assert(u.count==4);
 field.value.bytes.offset=121;dok_ui_tick(&u,&level,0);assert(u.errors==1 && !u.active);
 dok_ui_init(&u);dok_ui_notify(&u,65534,2,0);dok_ui_tick(&u,&level,0);assert(dok_ui_text(&u,&level,out)>0);
 memcpy(long_text,out,30);u.language=1;dok_ui_tick(&u,&level,0);dok_ui_text(&u,&level,out);assert(memcmp(long_text,out,10)!=0);
 memset(font8,0xff,sizeof(font8));memset(font16,0xff,sizeof(font16));
 world.player.health=-32768;world.player.energy=32767;world.player.lives=-32768;world.player.keys=65535;world.player.selected_weapon=8;
 for(unsigned state=0;state<=DOK_UI_MAP;state++)for(unsigned language=0;language<2;language++){
  memset(framebuffer,0xa5,sizeof(framebuffer));u.state=state;u.language=language;
  dok_ui_draw(&u,framebuffer+1,&world,font8,font16,255,0);
  assert(framebuffer[0]==0xa5 && framebuffer[38401]==0xa5);
 }
 /* Discovery fills the visible room and preserves the wall's occlusion.
  * Clearing visited on a fresh level must also invalidate the cell cache. */
 dok_runtime_definition map_defs[2]={{0},{0}};map_defs[0].class_id=1;map_defs[0].actor_index=0;map_defs[1].class_id=2;
 dok_runtime_level map_level={0};map_level.settings[DOK_LEVEL_WIDTH]=5;map_level.settings[DOK_LEVEL_LENGTH]=5;
 map_level.settings[DOK_LEVEL_CELL_SIDE]=480;map_level.cells=25;map_level.object_count=2;
 map_level.definitions=map_defs;map_level.glyph_index[' ']=1;map_level.glyph_index['#']=2;
 world.level=&map_level;world.object_count=2;world.actor_count=1;world.player_index=1;
 world.actors[0].x=720;world.actors[0].z=1200;memset(world.maps,0,sizeof(world.maps));memset(world.maps+25,' ',25);
 for(unsigned z=0;z<5;++z)world.maps[25+z*5+2]='#';
 for(unsigned run=0;run<2;++run){
  dok_ui_explore(&world);
  for(unsigned z=0;z<5;++z)for(unsigned x=0;x<5;++x)assert((world.maps[75+z*5+x]!=0)==(x<=2));
  memset(world.maps+75,0,25);
 }
 puts("G17 UI: keypad/chord/held-edge, localization, queue, 240-byte clamp, wraparound deadlines, all states framebuffer guards passed");return 0;
}
