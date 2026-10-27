#include "dok/ui.h"
extern dok_runtime g_render_world;
extern uint32_t gba_time_ticks(void);
extern void gba_time_pause(unsigned);
extern void gba_motion_hold(uint16_t);
extern void gba_flow_start(void);
extern unsigned gba_flow_screen(void);
extern unsigned gba_flow_input(uint16_t);
extern unsigned gba_flow_active(void);
extern void gba_flow_menu(void);
dok_ui g_ui;
static volatile uint16_t irq_keys,irq_pressed;
void gba_ui_vblank(void){
 uint16_t keys=(uint16_t)(~*(volatile uint16_t *)0x04000130)&0x3ff;
 irq_pressed|=(uint16_t)(keys&~irq_keys&(g_ui.state?0x3ff:4));
 irq_keys=keys;
}
volatile struct {uint32_t request,ack,op,arg,object,slot;} g_ui_probe;
volatile struct {uint32_t magic,state,focus,language,messages,object,slot,length,page,pages,remaining,draws,font8,font16,last_keys,errors;} g_ui_diag;
void gba_ui_start(void){dok_ui_init(&g_ui);g_ui_diag.magic=0x55493137;gba_time_pause(1);gba_flow_start();}
void gba_ui_notify(uint16_t object,uint16_t slot){dok_ui_notify(&g_ui,object,slot,gba_time_ticks());}
void gba_ui_system(unsigned kind){if(kind<7)gba_ui_notify(UINT16_MAX-1,(uint16_t)kind);}
unsigned gba_ui_input(uint16_t keys){
 /* Retain a one-VBlank tap while a GUI frame is being drawn. Game input
  * still uses the physical keypad and the existing release/block policy. */
 uint16_t ime=*(volatile uint16_t *)0x04000208;*(volatile uint16_t *)0x04000208=0;
 keys|=irq_pressed;
 irq_pressed=0;*(volatile uint16_t *)0x04000208=ime;
 unsigned changed=gba_flow_screen()?gba_flow_input(keys):dok_ui_input(&g_ui,keys);
 gba_flow_menu();
 if(g_ui_probe.request!=g_ui_probe.ack){
  switch(g_ui_probe.op){
   case 1:if(g_ui_probe.arg<=DOK_UI_MAP){g_ui.state=g_ui_probe.arg;g_ui.focus=0;g_ui.return_state=2;g_ui.blocked|=keys;}else ++g_ui.errors;break;
   case 2:if(g_ui_probe.arg<2){changed=g_ui.language!=g_ui_probe.arg;g_ui.language=g_ui_probe.arg;}else ++g_ui.errors;break;
   case 3:gba_ui_notify((uint16_t)g_ui_probe.object,(uint16_t)g_ui_probe.slot);break;
   case 4:{uint8_t b[DOK_UI_TEXT_LIMIT];for(unsigned i=0;i<sizeof(b);i++)b[i]=(uint8_t)('A'+i%26);dok_ui_literal(&g_ui,b,g_ui_probe.arg>sizeof(b)?sizeof(b):g_ui_probe.arg,gba_time_ticks());break;}
   case 5:dok_ui_tick(&g_ui,g_render_world.level,g_ui_probe.arg);break;
   default:++g_ui.errors;break;
  }
  g_ui_probe.ack=g_ui_probe.request;
 }
 gba_time_pause(!gba_flow_active());
 if(g_ui.state)gba_motion_hold(keys);
 return changed;
}
uint16_t gba_ui_keys(uint16_t keys){return dok_ui_game_keys(&g_ui,keys);}
unsigned gba_ui_language(void){return g_ui.language;}
void gba_ui_snapshot(const uint8_t *font8,const uint8_t *font16){
 g_ui_diag.state=g_ui.state;g_ui_diag.focus=g_ui.focus;g_ui_diag.language=g_ui.language;g_ui_diag.messages=g_ui.messages;
 g_ui_diag.object=g_ui.object;g_ui_diag.slot=g_ui.slot;g_ui_diag.length=g_ui.active?g_ui.length:0;
 g_ui_diag.page=g_ui.page;g_ui_diag.pages=g_ui.pages;g_ui_diag.remaining=g_ui.active?g_ui.deadline-gba_time_ticks():0;
 ++g_ui_diag.draws;g_ui_diag.font8=font8!=0;g_ui_diag.font16=font16!=0;g_ui_diag.last_keys=g_ui.last_keys;g_ui_diag.errors=g_ui.errors;
}
void gba_ui_draw(uint8_t *frame,const uint8_t *font8,const uint8_t *font16,unsigned white,unsigned black){
 dok_ui_tick(&g_ui,g_render_world.level,gba_time_ticks());
 dok_ui_draw(&g_ui,frame,&g_render_world,font8,font16,white,black);
 gba_ui_snapshot(font8,font16);
}

void gba_ui_reset(void){unsigned language=g_ui.language,start=g_ui.start_level;uint32_t last=g_ui.last_keys,blocked=g_ui.blocked;
 dok_ui_init(&g_ui);g_ui.language=language;g_ui.start_level=start;g_ui.last_keys=last;g_ui.blocked=blocked|last;g_ui.state=DOK_UI_PLAY;}
