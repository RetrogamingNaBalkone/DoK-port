#include "dok/flow.h"
#include "dok/ui.h"
#include "dok/rom_assets.h"
extern dok_runtime g_render_world;
extern dok_ui g_ui;
extern uint32_t gba_time_ticks(void);
extern void gba_time_pause(unsigned);
extern void gba_motion_hold(uint16_t);
extern void gba_flow_load(unsigned,unsigned);
extern void gba_flow_quiet(void);
extern void gba_audio_event(uint16_t,uint16_t,unsigned);
extern void gba_audio_menu(unsigned);
extern const dok_rom_assets *gba_flow_assets(void);
extern unsigned gba_flow_level(void);
enum {TITLE,INTRO,PLAY,DEATH,OUTRO,FINAL,GAMEOVER,STORY};
volatile struct {uint32_t request,ack,op,arg;} g_flow_probe;
volatile struct {uint32_t magic,state,level,session,lives,score,transitions,resets,pending,text_id,text_size,page,pages,errors;} g_flow_diag;
static unsigned state,session,pending,page,resume,legacy;
static uint16_t previous,blocked;
static dok_rom_resource screen;
static unsigned screen_id;
static void change(unsigned value){state=value;page=0;++g_flow_diag.transitions;blocked|=previous;}
static void select_text(void){
 const char *name=0;char buf[]="BINTRO1";
 if(state==INTRO || state==OUTRO){buf[0]=state==INTRO?'B':'E';buf[6]=(char)('1'+gba_flow_level());name=buf;}
 else if(state==FINAL)name="ENDTXT";
 else if(state==STORY)name="README";
 screen=(dok_rom_resource){0};screen_id=UINT16_MAX;
 if(name){uint16_t id;if(dok_rom_find(&id,gba_flow_assets(),(const uint8_t *)name,(state==FINAL || state==STORY)?6:7)==DOK_ROM_OK && dok_rom_resource_at(&screen,gba_flow_assets(),id,g_ui.language)==DOK_ROM_OK)screen_id=id;else ++g_flow_diag.errors;}
}
void gba_flow_start(void){g_flow_diag.magic=0x464c3138;state=TITLE;resume=PLAY;}
unsigned gba_flow_screen(void){return state!=TITLE && state!=PLAY;}
unsigned gba_flow_active(void){return (legacy || state==PLAY) && !g_ui.state;}
unsigned gba_flow_abort(void){return !legacy && (pending || g_render_world.player.health<=0);}
void gba_flow_finish(void){if(!legacy && state==PLAY)pending=2;}
void gba_flow_menu(void){
 previous=(uint16_t)g_ui.last_keys;blocked&=previous;
 if(legacy){g_ui.action=0;return;}
 if(g_ui.action){unsigned action=g_ui.action;g_ui.action=0;
  if(action==DOK_UI_STORY){change(STORY);g_ui.state=DOK_UI_TITLE;select_text();}
  else if(action==DOK_UI_CREDITS){change(FINAL);g_ui.state=DOK_UI_TITLE;select_text();}
  else if(action==DOK_UI_NEW_GAME){pending=1;g_ui.state=DOK_UI_TITLE;}
  else {change(resume);g_ui.state=DOK_UI_PLAY;select_text();}
 }
 if(state==PLAY && g_ui.state==DOK_UI_TITLE){resume=PLAY;change(TITLE);}
}
unsigned gba_flow_input(uint16_t keys){
 unsigned pressed=keys&~previous,changed=0;previous=keys;blocked&=keys;pressed&=~blocked;
 g_ui.last_keys=keys;
 if(pressed&4){g_ui.language^=1;changed=1;select_text();}
 if((pressed&2) && page){--page;}
 else if(pressed&2){resume=state;change(TITLE);g_ui.state=DOK_UI_TITLE;g_ui.focus=resume==STORY?3:resume==FINAL?4:0;g_ui.blocked|=keys;}
 else if(pressed&(1|8)){
  unsigned pages=state==FINAL?2:screen.data?dok_ui_screen_pages(screen.data,screen.size):1;
  if(page+1<pages)++page;
  else if(state==INTRO){change(PLAY);g_ui.state=DOK_UI_PLAY;g_ui.blocked|=keys;}
  else if(state==DEATH){pending=3;}
  else if(state==OUTRO){session=0;resume=PLAY;change(TITLE);g_ui.state=DOK_UI_LEVELS;g_ui.focus=g_ui.start_level;g_ui.blocked|=keys;select_text();}
  else {unsigned menu_text=state==STORY || state==FINAL,focus=state==STORY?3:state==FINAL?4:0;if(!menu_text)session=0;resume=PLAY;change(TITLE);g_ui.state=DOK_UI_TITLE;g_ui.focus=focus;g_ui.blocked|=keys;}
 }
 return changed;
}
void gba_flow_update(void){
 if(g_flow_probe.request!=g_flow_probe.ack){
  if(g_flow_probe.op==3){if(!g_render_world.level)gba_flow_load(0,0);legacy=1;state=PLAY;session=1;pending=0;g_ui.state=DOK_UI_PLAY;g_ui.action=0;}
  else ++g_flow_diag.errors;
  g_flow_probe.ack=g_flow_probe.request;
 }
 if(!legacy){
  if(state==PLAY && g_render_world.player.health<0 && !pending)pending=2;
  if(pending==1){legacy=0;session=1;gba_flow_load(g_ui.start_level,0);++g_flow_diag.resets;change(INTRO);g_ui.state=DOK_UI_PLAY;select_text();}
  else if(pending==2){gba_flow_quiet();change(OUTRO);resume=OUTRO;select_text();}
  else if(pending==3){if(!dok_flow_retry(&g_render_world))++g_flow_diag.errors;change(PLAY);g_ui.state=DOK_UI_PLAY;g_ui.blocked|=previous;gba_motion_hold(previous);}
  pending=0;
  if(state==PLAY && g_render_world.player.health==0){
   g_render_world.player.lives=(int16_t)(uint16_t)((uint16_t)g_render_world.player.lives-1u);
   gba_audio_event((uint16_t)(g_render_world.player_index-1),0xa6,0);change(g_render_world.player.lives!=0?DEATH:GAMEOVER);resume=state;if(state==GAMEOVER){session=0;gba_flow_quiet();}select_text();
  }
 }
 gba_audio_menu(!legacy && ((state==TITLE && dok_ui_frontend(&g_ui)) || state==STORY || state==FINAL));
 gba_time_pause(!gba_flow_active());
 if(!gba_flow_active())gba_motion_hold(previous);
 g_flow_diag.state=state;g_flow_diag.level=gba_flow_level();g_flow_diag.session=session;
 g_flow_diag.lives=(uint32_t)(int32_t)g_render_world.player.lives;g_flow_diag.score=(uint32_t)(int32_t)g_render_world.player.score;
 g_flow_diag.pending=pending;g_flow_diag.text_id=screen_id;g_flow_diag.text_size=screen.size;g_flow_diag.page=page;
 g_flow_diag.pages=state==FINAL?2:screen.data?dok_ui_screen_pages(screen.data,screen.size):1;
}
void gba_flow_draw(uint8_t *f,const uint8_t *f8,const uint8_t *f16,unsigned fg,unsigned bg){
 if(!gba_flow_screen())return;
 static const char *const headings[2][5]={{"\202\221\222\223\217\213\205\215\210\205","\223\220\216\202\205\215\234 \217\220\216\211\204\205\215","\212\216\215\205\226","\217\216\202\222\216\220","\210\203\220\200 \216\212\216\215\227\205\215\200"},{"INTRO","LEVEL COMPLETE","THE END","RETRY","GAME OVER"}};
 static const uint8_t retry_ru[]={0x8f,0x8e,0x82,0x92,0x8e,0x90,0x20,0x8f,0x8e,0x8f,0x9b,0x92,0x8a,0x88};
 static const uint8_t over_ru[]={0x88,0x83,0x90,0x80,0x20,0x8e,0x8a,0x8e,0x8d,0x97,0x85,0x8d,0x80};
 const uint8_t *text=screen.data;size_t size=screen.size;
 if(state==FINAL){dok_ui_credits(f,f8,f16,fg,bg,text,size,page,g_ui.language);return;}
 if(!text){if(state==DEATH){text=g_ui.language?(const uint8_t *)"TRY AGAIN":retry_ru;size=g_ui.language?9:sizeof(retry_ru);}else{text=g_ui.language?(const uint8_t *)"GAME OVER":over_ru;size=g_ui.language?9:sizeof(over_ru);}}
 dok_ui_screen(f,f8,f16,fg,bg,text,size,page,state==STORY?(g_ui.language?"STORY":"\217\220\205\204\233\221\222\216\220\210\237"):headings[g_ui.language][state==INTRO?0:state==OUTRO?1:state==FINAL?2:state==DEATH?3:4],g_ui.language);
}
