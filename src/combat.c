#include "dok/combat.h"
#include "dok/cell_coordinate.h"
#include "dok/movement.h"
#include "dok/animation.h"
static int16_t word(int32_t x) {uint16_t n=(uint16_t)x;return n<32768?(int16_t)n:(int16_t)((int32_t)n-65536);}
static int cell(dok_combat *,int32_t,int32_t);
static void unoccupy(dok_combat *c,dok_combat_projectile *p) {
 if(p->state&0x40) {int pos=cell(c,p->x,p->z);if(pos>=0)c->occupied[pos>>3]&=(uint8_t)~(1u<<(pos&7));p->state&=0xbf;}
}
static int32_t sar(int32_t n,unsigned b) {return n>=0?n/(1<<b):-1-((-1-n)/(1<<b));}
static void emit(dok_combat *c,dok_combat_kind k,uint16_t s,uint16_t t,int16_t n) {if(c->sink)c->sink(c->context,k,s,t,n);}
static int animation(dok_combat *c,uint16_t resource,uint16_t index,dok_animation_frame *f,uint16_t *count) {
 dok_rom_resource r;dok_animation_view a;
 if(!resource || dok_rom_resource_at(&r,c->assets,resource,c->language)!=DOK_ROM_OK || dok_animation_open(r.data,r.size,&a)!=DOK_ASSET_OK) return 0;
 *count=a.frame_count;return dok_animation_frame_at(&a,index,f)==DOK_ASSET_OK;
}
static uint16_t binding(dok_combat *c,uint16_t object,uint16_t slot) {
 uint16_t id=0;dok_rom_binding(&id,c->assets,c->level,c->language,object,slot);return id;
}
static uint16_t weapon(dok_combat *c) {
 int s=c->world->player.selected_weapon;return s>=0 && s<9?c->world->player.weapons[s]:0;
}
void dok_combat_init(dok_combat *c,dok_runtime *w,const dok_rom_assets *a,dok_items *items,unsigned level,unsigned language,uint32_t tick,dok_combat_sink sink,void *context) {
 *c=(dok_combat){0};c->world=w;c->assets=a;c->items=items;c->level=level;c->language=language;c->now=tick;c->sink=sink;c->context=context;
 c->cursor=DOK_RUNTIME_NONE;c->weapon_state=7;c->weapon_deadline=tick+1100;
 c->current=c->pending_first=c->pending_last=DOK_RUNTIME_NONE;
 c->displayed_weapon=weapon(c);
 for(unsigned i=0;i<256;i++)c->slots[i].next=c->slots[i].previous=DOK_RUNTIME_NONE;
}
void dok_combat_remove(dok_combat *c,uint16_t id) {
 if(id>=256 || !c->slots[id].state)return;dok_combat_projectile *p=&c->slots[id];
 unoccupy(c,p);
 if(p->state&0x80) {
  uint16_t at=c->pending_first,previous=DOK_RUNTIME_NONE;
  while(at!=DOK_RUNTIME_NONE && at!=id) {previous=at;at=c->slots[at].next;}
  if(at==id) {if(previous==DOK_RUNTIME_NONE)c->pending_first=p->next;else c->slots[previous].next=p->next;
   if(c->pending_last==id)c->pending_last=previous;}
  p->state=0;p->next=p->previous=DOK_RUNTIME_NONE;++c->removed;emit(c,DOK_COMBAT_REMOVED,id,DOK_RUNTIME_NONE,0);return;
 }
 if(c->dispatching && id==c->current) {
  p->state=0;++c->removed;emit(c,DOK_COMBAT_REMOVED,id,DOK_RUNTIME_NONE,0);return;
 }
 if(c->active>1) {c->slots[p->next].previous=p->previous;c->slots[p->previous].next=p->next;}
 if(c->cursor==id)c->cursor=p->next;if(!--c->active)c->cursor=DOK_RUNTIME_NONE;
 p->state=0;p->next=p->previous=DOK_RUNTIME_NONE;++c->removed;emit(c,DOK_COMBAT_REMOVED,id,DOK_RUNTIME_NONE,0);
}
void dok_combat_clear(dok_combat *c) {for(unsigned i=0;i<256;i++)dok_combat_remove(c,(uint16_t)i);}
static int free_slot(dok_combat *c) {
 for(unsigned i=0;i<256;i++) {
  /* Native glyph table is occupied by definitions before dynamic objects. */
  unsigned token=c->world->level->glyph_index[i];
  if(i!=c->current && !c->slots[i].state && (!token || (c->world->objects[token-1].flags&0x400)))return (int)i;
 }return -1;
}
int dok_combat_spawn(dok_combat *c,uint16_t owner,uint8_t glyph) {
 dok_runtime *w=c->world;const dok_runtime_actor *a=dok_runtime_actor_at(w,owner);
 unsigned token=w->level->glyph_index[glyph];int id=free_slot(c);
 if(id<0) {emit(c,DOK_COMBAT_FULL,DOK_RUNTIME_NONE,owner,0);return 0;}
 if(!a || !token || w->level->definitions[token-1].class_id!=8)return 0;
 unsigned old=w->level->glyph_index[id];
 if(old)emit(c,DOK_COMBAT_RECLAIM,(uint16_t)id,(uint16_t)(old-1),0);
 const dok_runtime_projectile *t=&w->objects[token-1].state.projectile;
 dok_combat_projectile *p=&c->slots[id];*p=(dok_combat_projectile){0};
 p->x=a->x;p->z=a->z;p->y=(int16_t)t->y;p->heading=a->heading;p->object=(uint16_t)(token-1);p->owner=owner;
 p->damage=t->damage;p->floor=t->half_damage;p->glyph=(uint8_t)id;p->state=1;p->last=c->now;p->deadline=c->now+416;
 p->resource=binding(c,p->object,DOK_ROM_SLOT_ANIMATION);
 dok_animation_frame f;uint16_t count;if(!animation(c,p->resource,0,&f,&count)) {p->state=0;return 0;}
 if(c->dispatching) {
  p->state|=0x80;p->next=DOK_RUNTIME_NONE;p->previous=DOK_RUNTIME_NONE;
  if(c->pending_last!=DOK_RUNTIME_NONE)c->slots[c->pending_last].next=(uint16_t)id;else c->pending_first=(uint16_t)id;
  c->pending_last=(uint16_t)id;return (int)p->damage;
 }
 if(!c->active)p->next=p->previous=(uint16_t)id;
 else {uint16_t head=c->cursor,next=c->slots[head].next;p->next=next;p->previous=head;c->slots[next].previous=(uint16_t)id;c->slots[head].next=(uint16_t)id;}
 c->cursor=(uint16_t)id;++c->active;return (int)p->damage;
}
int dok_combat_request(dok_combat *c,int16_t slot) {
 if(slot<0 || slot>=9 || !c->world->player.weapons[slot])return 0;
 c->world->player.pending_weapon=slot;return 1;
}
int dok_combat_fire(dok_combat *c) {
 dok_runtime_player *p=&c->world->player;uint16_t token=weapon(c);
 if(!token || (c->weapon_state!=7 && c->weapon_state!=3))return 0;
 if(!p->energy) {emit(c,DOK_COMBAT_EMPTY,DOK_RUNTIME_NONE,token-1,0);return 0;}
 /* Native readiness changes before allocation, even if all slots are full. */
 c->weapon_state=2;c->weapon_resource=binding(c,token-1,DOK_ROM_SLOT_ANIMATION);
 int damage=dok_combat_spawn(c,(uint16_t)(c->world->player_index-1),(uint8_t)('0'+p->selected_weapon));
 if(!damage)return 0;
 dok_items_stat(c->items,1,(int16_t)-damage);++c->shots;
 emit(c,DOK_COMBAT_SHOT,c->cursor,token-1,(int16_t)damage);return 1;
}
int dok_combat_damage(dok_combat *c,uint16_t target,uint16_t source,int16_t damage) {
 dok_runtime *w=c->world;if(target>=w->object_count)return 0;
 unsigned cls=w->level->definitions[target].class_id;
 if(cls!=1 && cls!=9)return 0;
 if(source<w->object_count && cls==w->level->definitions[source].class_id)return 0;
 int16_t health=cls==1?w->player.health:w->objects[target].state.monster.health;
 if(!health)return cls==1;
 if(cls==1) {dok_items_stat(c->items,0,word(-damage));health=w->player.health;}
 else {
  /* Native first contact wakes state0 without subtracting health. G15 owns
   * the wake/hurt/AI transitions; visual completion cannot perform them. */
  if(!w->objects[target].state.monster.state) {emit(c,DOK_COMBAT_WAKE,source,target,0);return 1;}
  health=word((int32_t)health-damage);if(health<0)health=0;w->objects[target].state.monster.health=health;
 }
 emit(c,DOK_COMBAT_DAMAGE,source,target,damage);if(health<=0)emit(c,DOK_COMBAT_DEATH,source,target,health);return 1;
}
static int cell(dok_combat *c,int32_t x,int32_t z) {
 const dok_runtime_level *l=c->world->level;int side=l->settings[DOK_LEVEL_CELL_SIDE];
#ifdef DOK_RENDERER_PROBE
 if(side==480 && l->settings[DOK_LEVEL_WIDTH]==64 && l->settings[DOK_LEVEL_LENGTH]==64){
  if((uint32_t)x>=30720 || (uint32_t)z>=30720)return -1;
  return dok_cell_480[(uint32_t)x>>4]+((unsigned)dok_cell_480[(uint32_t)z>>4]<<6);
 }
#endif
 int cx=x/side,cz=z/side;if(x<0 || z<0 || cx>=l->settings[DOK_LEVEL_WIDTH] || cz>=l->settings[DOK_LEVEL_LENGTH])return -1;
 return cz*l->settings[DOK_LEVEL_WIDTH]+cx;
}
int dok_combat_blocked(void *context,int32_t x,int32_t z) {
 dok_combat *c=context;int pos=cell(c,x,z);return pos>=0 && (c->occupied[pos>>3]&(1u<<(pos&7)))!=0;
}
uint16_t dok_combat_object_at(const dok_combat *c,int32_t x,int32_t z) {
 if(!c || !c->world)return DOK_RUNTIME_NONE;
 int pos=cell((dok_combat *)c,x,z);
 if(pos<0 || !(c->occupied[pos>>3]&(1u<<(pos&7))))return DOK_RUNTIME_NONE;
 for(unsigned i=0;i<256;i++){
  const dok_combat_projectile *p=&c->slots[i];
  if((p->state&0x40) && cell((dok_combat *)c,p->x,p->z)==pos)return p->object;
 }
 return DOK_RUNTIME_NONE;
}
static void occupy(dok_combat *c,dok_combat_projectile *p) {
 int pos=cell(c,p->x,p->z);if(pos<0 || c->world->maps[pos]!=' ' || dok_combat_blocked(c,p->x,p->z))return;
 c->occupied[pos>>3]|=(uint8_t)(1u<<(pos&7));p->state|=0x40;
}
static int target(dok_combat *c,dok_combat_projectile *p) {
 dok_runtime *w=c->world;int pos=cell(c,p->x,p->z);if(pos<0)return -2;
 /* Test actors before walls, mirroring native MMap then TMap fallback. */
 for(unsigned i=0;i<w->object_count;i++) {
  unsigned cls=w->level->definitions[i].class_id;if((cls!=1 && cls!=9) || (w->objects[i].flags&0x400))continue;
  if(cls==9 && !w->objects[i].state.monster.health)continue;
  const dok_runtime_actor *a=dok_runtime_actor_at(w,(uint16_t)i);
  if(a && cell(c,a->x,a->z)==pos && cls!=w->level->definitions[p->owner].class_id)return (int)i;
 }
 unsigned tok=w->level->glyph_index[w->maps[w->level->cells+pos]];
 if(tok) {unsigned cls=w->level->definitions[tok-1].class_id,flags=w->objects[tok-1].flags;
  if((cls==7 && !(flags&8)) || (cls!=7 && !(flags&0x18)))return (int)(tok-1);}
 return -1;
}
static void update(dok_combat *c,uint16_t id) {
 dok_combat_projectile *p=&c->slots[id];dok_animation_frame f;uint16_t count;
 if(!animation(c,p->resource,p->frame,&f,&count)) {++c->errors;dok_combat_remove(c,id);return;}
 if((p->state&15)>=2) {
  if((p->state&15)==2) {p->resource=p->reserved;p->frame=0;p->state=(uint8_t)((p->state&0xf0)|3);
   if(!animation(c,p->resource,0,&f,&count)) {++c->errors;dok_combat_remove(c,id);return;}}
  if((unsigned)p->frame+1>=count) {dok_combat_remove(c,id);return;}
  ++p->frame;animation(c,p->resource,p->frame,&f,&count);p->deadline=c->now+(uint32_t)(int32_t)f.interval_ticks;return;
 }
 unoccupy(c,p);
 int elapsed=(int)(((c->now-p->last)*256u)/416u);if(elapsed<256)elapsed=256;if(elapsed>2048)elapsed=2048;
 int speed=(int)c->world->objects[p->object].state.projectile.velocity;
 int distance=sar(word(speed)*elapsed,8),quarter=-(int)c->world->level->settings[DOK_LEVEL_CELL_SIDE]/4;
 int whole=distance/quarter,remainder=distance%quarter;int hit=-1;
 for(int step=0;step<=whole;step++) {
  dok_runtime_actor a={0};a.x=p->x;a.z=p->z;a.heading=p->heading;
  dok_motion_free(&a,(int16_t)(step==0?remainder:quarter),0,0);p->x=a.x;p->z=a.z;
  hit=target(c,p);if(hit!=-1)break;
 }
 if(hit!=-1) {
  int impact=hit>=0 && dok_combat_damage(c,(uint16_t)hit,p->owner,p->damage);
  p->reserved=binding(c,p->object,impact?0x77:0x7b);p->frame=0;p->state=2;
  /* Native backs one quarter-cell out of the impact and adjusts vertical
   * placement from the target height and the still displayed flight image. */
  dok_runtime_actor back={0};back.x=p->x;back.z=p->z;back.heading=p->heading;
  dok_motion_free(&back,(int16_t)-quarter,0,0);p->x=back.x;p->z=back.z;
  if(hit>=0 && c->world->level->definitions[hit].height_parameter>0)
   p->y=word(-c->world->level->definitions[hit].height_parameter+(f.image.height*c->world->level->definitions[p->object].width_parameter>>1));
  if(impact) {++c->hits;emit(c,DOK_COMBAT_IMPACT,id,(uint16_t)hit,p->damage);}
  else {++c->misses;emit(c,DOK_COMBAT_MISS,id,hit<0?DOK_RUNTIME_NONE:(uint16_t)hit,0);}
  animation(c,p->resource,0,&f,&count);
 } else {p->frame=(uint16_t)((p->frame+1)%count);animation(c,p->resource,p->frame,&f,&count);}
 p->damage=(int16_t)sar(word((int32_t)p->damage+p->floor),1);
 occupy(c,p);
 p->last=c->now;p->deadline=c->now+(uint32_t)(int32_t)f.interval_ticks;
}
static void weapon_update(dok_combat *c) {
 dok_runtime_player *p=&c->world->player;uint16_t token=weapon(c);
 if(!token)return;
 if(!c->displayed_weapon) {
  c->displayed_weapon=token;c->weapon_state=1;c->weapon_shift=2;c->weapon_frame=0;c->weapon_deadline=c->now+1100;
  emit(c,DOK_COMBAT_WEAPON,DOK_RUNTIME_NONE,token-1,p->selected_weapon);return;
 }
 if(p->pending_weapon!=p->selected_weapon && c->weapon_state!=0 && c->weapon_state!=4) {c->weapon_state=0;c->weapon_shift=0;c->weapon_frame=0;}
 if(c->now<c->weapon_deadline)return;c->weapon_deadline=c->now+1100;
 if(c->weapon_state==0) {if(++c->weapon_shift==3)c->weapon_state=4;return;}
 if(c->weapon_state==4) {p->selected_weapon=p->pending_weapon;c->displayed_weapon=weapon(c);c->weapon_state=1;c->weapon_shift=2;c->weapon_frame=0;emit(c,DOK_COMBAT_WEAPON,DOK_RUNTIME_NONE,weapon(c)-1,p->selected_weapon);return;}
 if(c->weapon_state==1) {if(!--c->weapon_shift)c->weapon_state=5;return;}
 c->weapon_resource=binding(c,token-1,DOK_ROM_SLOT_ANIMATION);
 dok_animation_frame f;uint16_t count;if(!animation(c,c->weapon_resource,c->weapon_frame,&f,&count)) {++c->errors;return;}
 if(c->weapon_state==2) {if(c->weapon_frame>=count-1)c->weapon_state=6;else ++c->weapon_frame;}
 else {c->weapon_state=3;if(!c->weapon_frame)c->weapon_state=7;else if(c->weapon_frame>=count-1)c->weapon_frame=0;else ++c->weapon_frame;}
 animation(c,c->weapon_resource,c->weapon_frame,&f,&count);c->weapon_deadline=c->now+(uint32_t)(int32_t)f.interval_ticks;
}
void dok_combat_poll(dok_combat *c,uint32_t tick) {
 if(c->dispatching)return;
 c->now=tick;weapon_update(c);
 c->dispatching=1;
 for(unsigned visit=0;visit<c->active;visit++) {
  uint16_t id=c->cursor,next=c->slots[id].next;c->current=id;
  if(tick>=c->slots[id].deadline)update(c,id);
  next=c->slots[id].next;
  if(!c->slots[id].state) {
   dok_combat_projectile *p=&c->slots[id];
   if(c->active>1) {c->slots[p->next].previous=p->previous;c->slots[p->previous].next=p->next;}
   if(!--c->active)c->cursor=DOK_RUNTIME_NONE;
   p->next=p->previous=DOK_RUNTIME_NONE;
  }
  if(c->active)c->cursor=next;
 }
 c->dispatching=0;c->current=DOK_RUNTIME_NONE;
 uint16_t id=c->pending_first;
 while(id!=DOK_RUNTIME_NONE) {
  dok_combat_projectile *p=&c->slots[id];uint16_t next=p->next;p->state&=0x7f;
  if(!c->active)p->next=p->previous=id;
  else {uint16_t head=c->cursor,n=c->slots[head].next;p->next=n;p->previous=head;c->slots[n].previous=id;c->slots[head].next=id;}
  c->cursor=id;++c->active;id=next;
 }
 c->pending_first=c->pending_last=DOK_RUNTIME_NONE;
}
