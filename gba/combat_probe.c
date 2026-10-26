#include "dok/combat.h"
#include "dok/renderer.h"
#include "dok/animation.h"
#include "dok/movement.h"
#include "dok/collision.h"
extern dok_items *gba_items_context(void);
extern uint32_t gba_time_ticks(void);
extern void gba_visual_remove(uint16_t);
dok_combat g_combat;
/* Commit request last. op:1 equip,2 request,3 fire,4 poll count quanta,
 * 5 spawn(owner=a,glyph=b),6 clear,7 damage(target=a,source=b,value=c),
 * 8 fill free slots,9 remove(a),10 restart. freeze gates production combat. */
volatile struct {uint32_t freeze,request,ack,op,a,b,c,tick,count,slot;} g_combat_probe;
volatile struct {uint32_t magic,error,active,shots,removed,hits,misses,events,kind,target,value,result,
 selected,pending,charge,health,state,shift,frame,resource,now,slot,pstate,object,owner,damage,floor,x,y,z,deadline,next,previous;} g_combat_diag;
static void notice(void *context,dok_combat_kind k,uint16_t slot,uint16_t target,int16_t n) {
 (void)context;++g_combat_diag.events;g_combat_diag.kind=k;g_combat_diag.slot=slot;g_combat_diag.target=target;g_combat_diag.value=(uint32_t)(int32_t)n;
 if(k==DOK_COMBAT_RECLAIM)gba_visual_remove(target);
}
void gba_combat_start(dok_runtime *w,const dok_rom_assets *a,unsigned level,unsigned language) {
 dok_combat_init(&g_combat,w,a,gba_items_context(),level,language,gba_time_ticks(),notice,0);
 dok_collision_overlay(w,dok_combat_blocked,&g_combat);
 g_combat_diag.magic=0x434f4d42;
}
void gba_combat_poll(uint32_t tick,uint32_t commands) {
 if(!g_combat.world || g_combat_probe.freeze)return;
 dok_combat_poll(&g_combat,tick);if(commands&DOK_INPUT_FIRE)dok_combat_fire(&g_combat);
}
void gba_combat_cycle(void) {
 int selected=g_combat.world->player.selected_weapon;
 for(unsigned i=1;i<9;i++) {int slot=(selected+(int)i)%9;
  if(slot>=0 && g_combat.world->player.weapons[slot]) {dok_combat_request(&g_combat,(int16_t)slot);return;}}
}
void gba_combat_update(dok_renderer_workspace *w,unsigned language) {
 dok_combat *c=&g_combat;c->language=language;
 if(g_combat_probe.request!=g_combat_probe.ack) {
  unsigned a=g_combat_probe.a,b=g_combat_probe.b;int result=0;
  switch(g_combat_probe.op) {
   case 1: {
    unsigned glyph=a==2?'V':'W',token=c->world->level->glyph_index[glyph];
    if(a<9 && token) {c->world->player.weapons[a]=(uint16_t)token;c->world->player.selected_weapon=(int16_t)a;c->world->player.pending_weapon=(int16_t)a;
     c->world->player.energy=(int16_t)b;c->world->player.energy_max=1000;c->weapon_state=7;c->weapon_shift=0;c->weapon_frame=0;
     c->displayed_weapon=(uint16_t)token;
     c->weapon_deadline=c->now+1100;result=1;}break;
   }
   case 2:result=dok_combat_request(c,(int16_t)a);break;
   case 3:result=dok_combat_fire(c);break;
   case 4:for(unsigned i=0;i<g_combat_probe.count && i<4096;i++)dok_combat_poll(c,g_combat_probe.tick+i*a);result=1;break;
   case 5:result=dok_combat_spawn(c,(uint16_t)a,(uint8_t)b);break;
   case 6:dok_combat_clear(c);result=1;break;
   case 7:result=dok_combat_damage(c,(uint16_t)a,(uint16_t)b,(int16_t)g_combat_probe.c);break;
   case 8:for(unsigned i=0;i<256;i++) {int r=dok_combat_spawn(c,(uint16_t)(c->world->player_index-1),'1');if(!r)break;++result;}break;
   case 9:dok_combat_remove(c,(uint16_t)a);result=1;break;
   case 10:dok_combat_init(c,c->world,c->assets,c->items,c->level,c->language,g_combat_probe.tick,notice,0);result=1;break;
  }
  g_combat_diag.result=(uint32_t)result;g_combat_probe.ack=g_combat_probe.request;
 }
 g_combat_diag.error=c->errors;g_combat_diag.active=c->active;g_combat_diag.shots=c->shots;g_combat_diag.removed=c->removed;
 g_combat_diag.hits=c->hits;g_combat_diag.misses=c->misses;g_combat_diag.selected=c->world->player.selected_weapon;
 g_combat_diag.pending=c->world->player.pending_weapon;g_combat_diag.charge=c->world->player.energy;g_combat_diag.health=c->world->player.health;
 g_combat_diag.state=c->weapon_state;g_combat_diag.shift=c->weapon_shift;g_combat_diag.frame=c->weapon_frame;g_combat_diag.resource=c->weapon_resource;g_combat_diag.now=c->now;
 unsigned id=g_combat_probe.slot&255;dok_combat_projectile *p=&c->slots[id];
 g_combat_diag.pstate=p->state&15;g_combat_diag.object=p->object;g_combat_diag.owner=p->owner;g_combat_diag.damage=p->damage;g_combat_diag.floor=p->floor;
 g_combat_diag.x=p->x;g_combat_diag.y=(uint32_t)(int32_t)p->y;g_combat_diag.z=p->z;g_combat_diag.deadline=p->deadline;g_combat_diag.next=p->next;g_combat_diag.previous=p->previous;
 w->sprite_count=0;
 for(unsigned i=0;i<256;i++) if(c->slots[i].state) {
  p=&c->slots[i];unsigned n=w->sprite_count++;
  w->sprites[n]=(dok_renderer_sprite){p->x,p->y,p->z,p->object,0};w->sprite_resource[n]=p->resource;w->sprite_frame[n]=p->frame;
 }
}
void gba_combat_overlay(dok_renderer_workspace *w) {
 dok_combat *c=&g_combat;int slot=c->world->player.selected_weapon;
 if(slot<0 || slot>=9 || !c->world->player.weapons[slot])return;
 unsigned object=c->world->player.weapons[slot]-1;uint16_t id;dok_rom_resource r;dok_animation_view a;dok_animation_frame f;
 if(dok_rom_binding(&id,c->assets,c->level,c->language,(uint16_t)object,DOK_ROM_SLOT_ANIMATION)!=DOK_ROM_OK ||
    dok_rom_resource_at(&r,c->assets,id,c->language)!=DOK_ROM_OK || dok_animation_open(r.data,r.size,&a)!=DOK_ASSET_OK ||
    dok_animation_frame_at(&a,c->weapon_frame,&f)!=DOK_ASSET_OK)return;
 int width=f.image.width*3/4,height=f.image.height*9/10;if(width<1 || height<1)return;
 int left=(240-width)/2,top=144-(height>>c->weapon_shift);
 for(int y=0;y<height && y+top<144;y++)for(int x=0;x<width;x++) {
  if(x+left<0 || x+left>=240 || y+top<0)continue;
  uint8_t color=f.image.pixels[(unsigned)(y*f.image.height/height)*f.image.width+(unsigned)(x*f.image.width/width)];
  if(color)w->frame[(y+top)*240+x+left]=color;
 }
}
