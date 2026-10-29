#include "dok/ui.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
static dok_runtime world;
static uint8_t framebuffer[38402],font8[2048],font16[4096];
static void release(dok_ui *u){dok_ui_input(u,0);dok_ui_game_keys(u,0);}
static void screen_line(unsigned row,const char *expected){
 for(unsigned x=0;x<28;++x){unsigned ch=x<strlen(expected)?(uint8_t)expected[x]:32;
  for(unsigned bit=0;bit<8;++bit)assert((framebuffer[1+(24+row*8)*240+8+x*8+bit]==255)==!!(font8[ch*8]&(128u>>bit)));
 }
}
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
 dok_ui_init(&u);u.state=DOK_UI_TITLE;dok_ui_input(&u,64);assert(u.focus==4);release(&u);
 dok_ui_input(&u,128);assert(u.focus==0);release(&u);
 uint8_t long_text[1024];for(unsigned i=0;i<sizeof(long_text);i++)long_text[i]=(uint8_t)i;
 dok_ui_literal(&u,long_text,sizeof(long_text),0xfffff000u);assert(u.literal_size==240);
 dok_ui_tick(&u,0,0xfffff000u);assert(u.length==240 && u.pages==4);
 uint8_t out[60];assert(dok_ui_text(&u,0,out)==60);assert(out[0]==' ' && out[32]==32);
 for(unsigned i=0;i<4;i++)dok_ui_tick(&u,0,0xfffff000u+(i+1)*30000u);
 assert(!u.active);
 /* The renderer and page count share word/paragraph boundaries. Keep the
  * original 8px glyphs; commas/periods never become a line on their own. */
 for(unsigned ch=0;ch<256;++ch)font8[ch*8]=(uint8_t)ch;font8[32*8]=0;
 const char *wrap="abcdefghijklmnopqrstuvwxyz,\r\nfoo.\r\n\r\n\r\nabcdefghijklmnopqrstuvwx \250 next.$ignored";
 memset(framebuffer,0xa5,sizeof(framebuffer));
 dok_ui_screen(framebuffer+1,font8,font16,255,0,(const uint8_t *)wrap,strlen(wrap),0,"",0);
 screen_line(0,"abcdefghijklmnopqrstuvwxyz,");screen_line(1,"foo.");screen_line(2,"");
 screen_line(3,"abcdefghijklmnopqrstuvwx");screen_line(4,"\250 next.");
 assert(framebuffer[0]==0xa5 && framebuffer[38401]==0xa5);
 uint8_t pages[15*29];memset(pages,'X',sizeof(pages));for(unsigned i=0;i<15;++i)pages[i*29+28]='\n';
 assert(dok_ui_screen_pages(pages,14*29)==1 && dok_ui_screen_pages(pages,sizeof(pages))==2);
 assert(dok_ui_screen_pages(pages,0)==1);
 dok_runtime_field field={0xbe,DOK_LEVEL_TEXT,0,{.bytes={0,120}}};
 dok_runtime_definition def={0};def.field_count=1;
 dok_runtime_level level={0};level.object_count=1;level.definitions=&def;level.fields=&field;level.text=long_text;level.text_size=120;world.level=&level;
 dok_ui_notify(&u,0,0xbe,0);dok_ui_tick(&u,&level,0);assert(u.length==120 && u.pages==2);
 for(unsigned i=0;i<9;i++)dok_ui_notify(&u,0,(uint16_t)(0xbe + i),0);assert(u.count==4);
 field.value.bytes.offset=121;dok_ui_tick(&u,&level,0);assert(u.errors==1 && !u.active);
 dok_ui_init(&u);dok_ui_notify(&u,65534,2,0);dok_ui_tick(&u,&level,0);assert(dok_ui_text(&u,&level,out)>0);
 memcpy(long_text,out,30);u.language=1;dok_ui_tick(&u,&level,0);dok_ui_text(&u,&level,out);assert(memcmp(long_text,out,10)!=0);
 /* Exact full-message adaptations occur before pagination, only in RU. */
 const char *const original[2]={"\040\212\216\202\230 \206\210\202\216\211 \202\216\204\233 \204\200\213 10 \213\205\222 \206\210\207\215\210","\040\203\216\220\230\216\212 \204\216\201\220\216\211 \212\200\230\210 \204\200\213 15 \213\205\222 \206\210\207\215\210"};
 const char *const shortened[2]={"\206\210\202\200\237 \202\216\204\200 \204\200\213\200 10 \213\205\222 \206\210\207\215\210","\204\216\201\220\200\237 \212\200\230\200 \204\200\213\200 15 \213\205\222 \206\210\207\215\210"};
 field.value.bytes.offset=0;
 for(unsigned i=0;i<2;++i){
  level.text=(const uint8_t *)original[i];level.text_size=(uint16_t)strlen(original[i]);field.value.bytes.size=level.text_size;
  dok_ui_init(&u);dok_ui_notify(&u,0,0xbe,0);dok_ui_tick(&u,&level,0);
  assert(u.length==strlen(shortened[i]) && u.pages==1);
  assert(dok_ui_text(&u,&level,out)==strlen(shortened[i]) && !memcmp(out,shortened[i],u.length));
  u.language=1;dok_ui_tick(&u,&level,0);
  assert(dok_ui_text(&u,&level,out)==strlen(original[i]) && !memcmp(out,original[i],u.length));
  u.language=0;dok_ui_tick(&u,&level,0);assert(u.length==strlen(shortened[i]));
  /* A partial/prefix match must retain original bytes. */
  --field.value.bytes.size;dok_ui_tick(&u,&level,0);
  assert(dok_ui_text(&u,&level,out)==strlen(original[i])-1 && !memcmp(out,original[i],u.length));
 }
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
 /* The map item gates object symbols; scrolling never supplies game keys. */
 dok_runtime_definition symbols[4]={{0},{0},{0},{0}};
 symbols[0].class_id=1;symbols[0].actor_index=0;symbols[0].marker=7;
 symbols[1].class_id=0;symbols[2].class_id=3;symbols[2].marker=12;
 symbols[3].class_id=9;symbols[3].marker=10;
 map_level.definitions=symbols;map_level.object_count=4;world.object_count=4;
 map_level.glyph_index[' ']=2;map_level.glyph_index['i']=3;map_level.glyph_index['m']=4;
 memset(world.maps,' ',75);memset(world.maps+75,1,25);
 world.maps[25+1*5+3]='i';world.maps[25+1*5+4]='m';
 memset(font8,0,sizeof(font8));dok_ui_init(&u);u.state=DOK_UI_MAP;u.map_centered=1;u.map_x=6;u.map_y=6;
 world.player.map_revealed=0;dok_ui_draw(&u,framebuffer+1,&world,font8,font16,255,0);
 assert(framebuffer[1+70*240+118]==0 && framebuffer[1+70*240+115]==0);
 world.player.map_revealed=1;dok_ui_draw(&u,framebuffer+1,&world,font8,font16,255,0);
 assert(framebuffer[1+70*240+118]==12 && framebuffer[1+70*240+115]==10);
 world.objects[3].state.monster.state=4;dok_ui_draw(&u,framebuffer+1,&world,font8,font16,255,0);
 assert(framebuffer[1+70*240+115]==0);
 world.maps[75+1*5+3]=0;dok_ui_draw(&u,framebuffer+1,&world,font8,font16,255,0);
 assert(framebuffer[1+70*240+118]==0); /* item does not bypass exploration */
 int initial_x=u.map_x;dok_ui_input(&u,16);assert(u.map_x==initial_x+6 && !dok_ui_game_keys(&u,16));
 for(unsigned i=0;i<30;++i)dok_ui_input(&u,16);assert(u.map_x>initial_x+6);
 dok_ui_input(&u,18);assert(u.state==DOK_UI_PLAY && !dok_ui_game_keys(&u,18));release(&u);
 assert(dok_ui_game_keys(&u,16)==16);
 puts("G17 UI: keypad/chord/held-edge, localization, queue, 240-byte clamp, wraparound deadlines, all states framebuffer guards passed");return 0;
}
