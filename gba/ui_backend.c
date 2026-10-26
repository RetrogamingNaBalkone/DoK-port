#include "dok/ui.h"
extern dok_runtime g_render_world;
extern uint32_t gba_time_ticks(void);
extern void gba_time_pause(unsigned);
extern void gba_motion_hold(uint16_t);
dok_ui g_ui;
volatile struct {uint32_t request,ack,op,arg,object,slot;} g_ui_probe;
volatile struct {uint32_t magic,state,focus,language,messages,object,slot,length,page,pages,remaining,draws,font8,font16,last_keys,errors;} g_ui_diag;
void gba_ui_start(void){dok_ui_init(&g_ui);g_ui_diag.magic=0x55493137;gba_time_pause(1);}
void gba_ui_notify(uint16_t object,uint16_t slot){dok_ui_notify(&g_ui,object,slot,gba_time_ticks());}
void gba_ui_system(unsigned kind){if(kind<7)gba_ui_notify(UINT16_MAX-1,(uint16_t)kind);}
unsigned gba_ui_input(uint16_t keys){
 unsigned changed=dok_ui_input(&g_ui,keys);
 if(g_ui_probe.request!=g_ui_probe.ack){
  switch(g_ui_probe.op){
   case 1:if(g_ui_probe.arg<=4){g_ui.state=g_ui_probe.arg;g_ui.focus=0;g_ui.return_state=2;g_ui.blocked|=keys;}else ++g_ui.errors;break;
   case 2:if(g_ui_probe.arg<2){changed=g_ui.language!=g_ui_probe.arg;g_ui.language=g_ui_probe.arg;}else ++g_ui.errors;break;
   case 3:gba_ui_notify((uint16_t)g_ui_probe.object,(uint16_t)g_ui_probe.slot);break;
   case 4:{uint8_t b[DOK_UI_TEXT_LIMIT];for(unsigned i=0;i<sizeof(b);i++)b[i]=(uint8_t)('A'+i%26);dok_ui_literal(&g_ui,b,g_ui_probe.arg>sizeof(b)?sizeof(b):g_ui_probe.arg,gba_time_ticks());break;}
   case 5:dok_ui_tick(&g_ui,g_render_world.level,g_ui_probe.arg);break;
   default:++g_ui.errors;break;
  }
  g_ui_probe.ack=g_ui_probe.request;
 }
 gba_time_pause(g_ui.state!=0);
 if(g_ui.state)gba_motion_hold(keys);
 return changed;
}
uint16_t gba_ui_keys(uint16_t keys){return dok_ui_game_keys(&g_ui,keys);}
unsigned gba_ui_language(void){return g_ui.language;}
void gba_ui_draw(uint8_t *frame,const uint8_t *font8,const uint8_t *font16,unsigned white,unsigned black){
 dok_ui_tick(&g_ui,g_render_world.level,gba_time_ticks());
 dok_ui_draw(&g_ui,frame,&g_render_world,font8,font16,white,black);
 g_ui_diag.state=g_ui.state;g_ui_diag.focus=g_ui.focus;g_ui_diag.language=g_ui.language;g_ui_diag.messages=g_ui.messages;
 g_ui_diag.object=g_ui.object;g_ui_diag.slot=g_ui.slot;g_ui_diag.length=g_ui.active?g_ui.length:0;
 g_ui_diag.page=g_ui.page;g_ui_diag.pages=g_ui.pages;g_ui_diag.remaining=g_ui.active?g_ui.deadline-gba_time_ticks():0;
 ++g_ui_diag.draws;g_ui_diag.font8=font8!=0;g_ui_diag.font16=font16!=0;g_ui_diag.last_keys=g_ui.last_keys;g_ui_diag.errors=g_ui.errors;
}
