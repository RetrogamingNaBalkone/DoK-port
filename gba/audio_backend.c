#include "dok/audio.h"
#include "dok/rom_assets.h"
#define R16(a) (*(volatile uint16_t *)(a))
#define R32(a) (*(volatile uint32_t *)(a))
static dok_audio voice,cursor;
static const dok_rom_assets *assets;
static unsigned level,language;
static uint32_t fifo[8] __attribute__((aligned(4)));
static unsigned buffer;
static uint32_t remaining,chunk,last_irq,period,resource_id;
/* request committed last. op1 play,2 loop,3 stop(resource),4 query(resource),
 * 5 reset,6 force-stop; resource is stable ROM ID. result is Boolean. */
volatile struct {uint32_t request,ack,op,resource,result;} g_audio_probe;
volatile struct {uint32_t magic,active,resource,rate,period,position,loops,starts,rejected,stops,completed,irqs,underruns,max_gap,refill_cycles,ram_bytes,level_resets,requests,identity;} g_audio_diag;
static uint32_t ticks(void){uint32_t h,l,h2;do{h=R16(0x0400010c);l=R16(0x04000108);h2=R16(0x0400010c);}while(h!=h2);return(h<<16)|l;}
static void hardware_stop(void){R16(0x04000102)=0;R16(0x04000106)=0;R16(0x040000c6)=0;R16(0x04000082)=0x0804;R16(0x04000202)=0x210;g_audio_diag.active=0;}
static void fill(void){dok_audio_pull(&cursor,(int8_t *)&fifo[buffer*4],16);g_audio_diag.position=cursor.position;g_audio_diag.loops=cursor.loops;}
static void arm_dma(void){R16(0x040000c6)=0;R32(0x040000bc)=(uintptr_t)&fifo[buffer*4];R32(0x040000c0)=0x040000a0;R16(0x040000c4)=4;R16(0x040000c6)=0xf640;}
static void arm_count(void){chunk=remaining>65536?65536:remaining;R16(0x04000106)=0;R16(0x04000104)=(uint16_t)(0u-chunk);R16(0x04000106)=0xc4;}
static void sync(void){g_audio_diag.active=voice.active;g_audio_diag.resource=resource_id;g_audio_diag.identity=voice.resource;g_audio_diag.starts=voice.starts;g_audio_diag.rejected=voice.rejected;g_audio_diag.stops=voice.stops;}
static int play(uint16_t id,int loop,uint32_t identity){
 dok_rom_resource r;dok_sound_view s;
 if(dok_rom_resource_at(&r,assets,id,language)!=DOK_ROM_OK || dok_sound_open(r.data,r.size,&s)!=DOK_SOUND_OK)return 0;
 uint16_t old=R16(0x04000208);R16(0x04000208)=0;
 int ok=dok_audio_play(&voice,&s,identity,loop);
 if(ok){resource_id=id;cursor=voice;period=(16777216u+s.sample_rate/2)/s.sample_rate;
  hardware_stop();R16(0x04000084)=0x80;R16(0x04000080)=0;R16(0x04000088)=0x200;R16(0x04000082)=0x0b04;
  buffer=0;fill();for(unsigned i=0;i<4;i++)R32(0x040000a0)=fifo[i];fill();for(unsigned i=0;i<4;i++)R32(0x040000a0)=fifo[i];
  fill();buffer=1;fill();buffer=0;
  arm_dma();remaining=s.sample_count;arm_count();
  R16(0x04000100)=(uint16_t)(0u-period);last_irq=ticks();R16(0x04000102)=0x80;
  g_audio_diag.rate=s.sample_rate;g_audio_diag.period=period;
 }sync();R16(0x04000208)=old;return ok;
}
void gba_audio_irq(uint32_t flags){
 if(!voice.active)return;
 if(flags&0x10){remaining-=chunk;if(!remaining){if(voice.loop)remaining=voice.sound.sample_count;else{hardware_stop();voice.active=0;++g_audio_diag.completed;sync();return;}}arm_count();}
 if(flags&0x200){uint32_t start=ticks(),gap=start-last_irq;last_irq=start;++g_audio_diag.irqs;
  if(gap>g_audio_diag.max_gap)g_audio_diag.max_gap=gap;
  if(gap*64>period*32)++g_audio_diag.underruns;
  fill();buffer^=1;arm_dma();uint32_t cost=(ticks()-start)*64;if(cost>g_audio_diag.refill_cycles)g_audio_diag.refill_cycles=cost;
 }
}
void gba_audio_start(const dok_rom_assets *a,unsigned l,unsigned lang){
 uint16_t old=R16(0x04000208);R16(0x04000208)=0;hardware_stop();
 if(voice.active)dok_audio_stop(&voice,voice.resource);
 assets=a;level=l;language=lang;g_audio_diag.magic=0x41554431;
 g_audio_diag.ram_bytes=sizeof(voice)+sizeof(cursor)+sizeof(fifo);++g_audio_diag.level_resets;
 R16(0x04000200)|=0x210;sync();R16(0x04000208)=old;
}
void gba_audio_event(uint16_t object,uint16_t slot,unsigned mode){
 uint16_t id;if(!assets || dok_rom_binding(&id,assets,level,language,object,slot)!=DOK_ROM_OK)return;
 uint32_t identity=((uint32_t)object<<16)|slot;
 ++g_audio_diag.requests;
 if(mode==1){if(!dok_audio_playing(&voice,identity))play(id,0,identity);}
 else if(mode==2){if(!dok_audio_playing(&voice,identity))play(id,1,identity);}
 else if(mode==3){uint16_t old=R16(0x04000208);R16(0x04000208)=0;if(dok_audio_stop(&voice,identity))hardware_stop();sync();R16(0x04000208)=old;}
 else {if(mode==4){uint16_t old=R16(0x04000208);R16(0x04000208)=0;if(voice.active)dok_audio_stop(&voice,voice.resource);hardware_stop();sync();R16(0x04000208)=old;}play(id,0,identity);}
}
void gba_audio_projectile(uint16_t slot,uint16_t object,unsigned mode){
 uint16_t id;uint32_t identity=0x40000000u|((uint32_t)slot+1);
 if(!assets || dok_rom_binding(&id,assets,level,language,object,DOK_ROM_SLOT_SOUND)!=DOK_ROM_OK)return;
 if(mode==3){uint16_t old=R16(0x04000208);R16(0x04000208)=0;if(dok_audio_stop(&voice,identity))hardware_stop();sync();R16(0x04000208)=old;}
 else play(id,0,identity);
}
void gba_audio_update(unsigned lang){language=lang;
 if(g_audio_probe.request!=g_audio_probe.ack){int result=0;uint16_t id=(uint16_t)g_audio_probe.resource;
  uint32_t identity=0x80000000u|id;
  if(g_audio_probe.op==1 || g_audio_probe.op==2)result=play(id,g_audio_probe.op==2,identity);
  else if(g_audio_probe.op==4)result=dok_audio_playing(&voice,identity);
  else if(g_audio_probe.op>=7 && g_audio_probe.op<=10){
   uint32_t key=g_audio_probe.resource;uint16_t resource;
   if(dok_rom_binding(&resource,assets,level,language,(uint16_t)(key>>16),(uint16_t)key)==DOK_ROM_OK){
    if(g_audio_probe.op==7 || g_audio_probe.op==8)result=play(resource,g_audio_probe.op==8,key);
    else if(g_audio_probe.op==10)result=dok_audio_playing(&voice,key);
    else {uint16_t old=R16(0x04000208);R16(0x04000208)=0;result=dok_audio_stop(&voice,key);if(result)hardware_stop();sync();R16(0x04000208)=old;}
   }
  }
  else {uint16_t old=R16(0x04000208);R16(0x04000208)=0;
   if(g_audio_probe.op==3){result=dok_audio_stop(&voice,identity);if(result)hardware_stop();}
   else if(g_audio_probe.op==5 || g_audio_probe.op==6){if(voice.active)dok_audio_stop(&voice,voice.resource);hardware_stop();if(g_audio_probe.op==5)++g_audio_diag.level_resets;result=1;}
   sync();R16(0x04000208)=old;}
  g_audio_probe.result=(uint32_t)result;g_audio_probe.ack=g_audio_probe.request;
 }
}
