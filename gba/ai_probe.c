#include "dok/ai.h"
#include "dok/combat.h"
extern uint32_t gba_time_ticks(void);
dok_ai g_ai;
extern dok_combat g_combat;
extern void gba_audio_event(uint16_t,uint16_t,unsigned);
extern void gba_ui_notify(uint16_t,uint16_t);
static void audio_notice(void *p,dok_ai_kind k,uint16_t id){
 (void)p;if(k==DOK_AI_WAKE)gba_audio_event(id,DOK_ROM_SLOT_SOUND,1);
 else if(k==DOK_AI_CONTACT || k==DOK_AI_RANGED)gba_audio_event(id,0xf0,1);
 else if(k==DOK_AI_DYING)gba_audio_event(id,0xf4,4);
 else if(k==DOK_AI_DEAD){gba_audio_event(id,0xf4,3);gba_ui_notify(id,0xbe);}
}
/* Commit request last. op1 consider(object),2 wake(object),3 poll tick/count/stride,
 * 4 remove(object),5 restart. freeze gates only live polling/consideration. */
volatile struct {uint32_t freeze,request,ack,op,object,tick,count,stride;} g_ai_probe;
volatile struct {uint32_t magic,error,steps,wakes,courses,blocked,contacts,now,owners,object,state,counter,x,z,heading,frame,deadline,active,result,attacks,hurts,deaths,completed,melee,ranged,burst,timer,resource,health,flags;} g_ai_diag;
void gba_ai_start(dok_runtime *w,const dok_rom_assets *a,unsigned level,unsigned language){dok_ai_init(&g_ai,w,a,level,language,gba_time_ticks(),audio_notice,0);dok_ai_connect_combat(&g_ai,&g_combat);g_ai_diag.magic=0x41493134;}
void gba_ai_notice(unsigned kind,uint16_t id){dok_ai_notice(&g_ai,kind,id);}
void gba_ai_poll(uint32_t tick,const uint8_t *considered){
 if(g_ai_probe.freeze || !g_ai.world)return;
 g_ai.now=tick;
 if(considered)for(unsigned id=0;id<g_ai.world->object_count;id++)if(considered[id])dok_ai_consider(&g_ai,(uint16_t)id);
 dok_ai_poll(&g_ai,tick);
}
void gba_ai_update(unsigned language){
 dok_ai *a=&g_ai;a->language=language;
 if(g_ai_probe.request!=g_ai_probe.ack){unsigned id=g_ai_probe.object;int result=1;
  switch(g_ai_probe.op){
   case 1:dok_ai_consider(a,(uint16_t)id);break;
   case 2:result=dok_ai_wake(a,(uint16_t)id);break;
   case 3:for(unsigned i=0;i<g_ai_probe.count && i<4096;i++)dok_ai_poll(a,g_ai_probe.tick+i*g_ai_probe.stride);break;
   case 4:dok_ai_remove(a,(uint16_t)id);break;
   case 5:dok_ai_init(a,a->world,a->assets,a->level,a->language,g_ai_probe.tick,audio_notice,0);dok_ai_connect_combat(a,&g_combat);break;
   case 6:dok_ai_notice(a,g_ai_probe.tick,(uint16_t)id);break;
   default:result=0;break;
  }g_ai_diag.result=(uint32_t)result;g_ai_probe.ack=g_ai_probe.request;
 }
 unsigned id=g_ai_probe.object;if(id>=a->world->object_count)id=0;dok_runtime_object *o=&a->world->objects[id];const dok_runtime_actor *p=dok_runtime_actor_at(a->world,(uint16_t)id);
 g_ai_diag.error=a->errors;g_ai_diag.steps=a->steps;g_ai_diag.wakes=a->wakes;g_ai_diag.courses=a->courses;g_ai_diag.blocked=a->blocked;g_ai_diag.contacts=a->contacts;g_ai_diag.now=a->now;g_ai_diag.owners=a->scheduler.owner_count;
 unsigned index=a->world->level->definitions[id].actor_index;
 g_ai_diag.object=id;g_ai_diag.state=o->state.monster.state;g_ai_diag.counter=o->state.monster.course_counter;g_ai_diag.x=p?p->x:0;g_ai_diag.z=p?p->z:0;g_ai_diag.heading=p?p->heading:0;g_ai_diag.frame=o->frame;g_ai_diag.deadline=index<128?a->owners[index].events[0].deadline:0;g_ai_diag.active=index<128?a->owners[index].count:0;
 g_ai_diag.attacks=a->attacks;g_ai_diag.hurts=a->hurts;g_ai_diag.deaths=a->deaths;g_ai_diag.completed=a->completed;g_ai_diag.melee=a->melee;g_ai_diag.ranged=a->ranged;g_ai_diag.burst=o->state.monster.shot_count;g_ai_diag.timer=o->state.monster.burst_started;g_ai_diag.resource=o->visual_resource;g_ai_diag.health=o->state.monster.health;g_ai_diag.flags=o->flags;
}
