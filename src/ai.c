#include "dok/ai.h"
#include "dok/animation.h"
#include "ai_atan.inc"
static uint32_t now(void *p){return ((dok_ai *)p)->now;}
static void emit(dok_ai *a,dok_ai_kind k,uint16_t id){if(a->sink)a->sink(a->context,k,id);}
static int32_t field(dok_ai *a,uint16_t id,unsigned off){int32_t n=0;dok_runtime_read_s(a->world,id,off,off==0xba?4:(off==0xe7?1:2),&n);return n;}
static dok_runtime_actor *actor(dok_ai *a,uint16_t id){const dok_runtime_actor *p=dok_runtime_actor_at(a->world,id);return (dok_runtime_actor *)p;}
int dok_ai_near(int32_t dx,int32_t dz,int32_t distance){
 /* Valid maps fit signed32 subtraction/abs; use unsigned magnitude for diagnostics. */
 uint32_t x=dx<0?0u-(uint32_t)dx:(uint32_t)dx,z=dz<0?0u-(uint32_t)dz:(uint32_t)dz;
 return distance>0 && (x>z?x:z)<(uint32_t)distance;
}
int16_t dok_ai_heading(int32_t dx,int32_t dz){
 uint32_t x=dx<0?0u-(uint32_t)dx:(uint32_t)dx,z=dz<0?0u-(uint32_t)dz:(uint32_t)dz;
 int n=0,angle=0;
 if(x<z){n=angles[(uint32_t)(x*100u)/z];if(dz>0 && dx>=0)angle=n;else if(dz<0 && dx>=0)angle=180-n;else if(dz>0 && dx<0)angle=360-n;else angle=180+n;}
 else if(x){n=angles[(uint32_t)(z*100u)/x];if(dx>0 && dz>=0)angle=90-n;else if(dx<0 && dz>=0)angle=270+n;else if(dx>0 && dz<0)angle=90+n;else angle=270-n;}
 return dok_motion_heading(360-angle);
}
void dok_ai_remove(dok_ai *a,uint16_t id){if(!a || !a->world || id>=a->world->object_count)return;unsigned index=a->world->level->definitions[id].actor_index;if(index>=DOK_RUNTIME_MAX_ACTORS)return;dok_scheduler_destroy(&a->scheduler,(uint16_t)index);a->last[index]=0;}
void dok_ai_init(dok_ai *a,dok_runtime *w,const dok_rom_assets *assets,unsigned level,unsigned language,uint32_t tick,dok_ai_sink sink,void *context){
 *a=(dok_ai){0};a->world=w;a->assets=assets;a->level=level;a->language=language;a->now=tick;a->sink=sink;a->context=context;
 dok_scheduler_init(&a->scheduler,a->owners,DOK_RUNTIME_MAX_ACTORS,now,a);
}
int dok_ai_wake(dok_ai *a,uint16_t id){
 if(!a || !a->world || id>=a->world->object_count || a->world->level->definitions[id].class_id!=9 || !actor(a,id))return 0;
 dok_runtime_object *o=&a->world->objects[id];if(o->state.monster.state || o->state.monster.health<=0 || (o->flags&0x400))return 0;
 /* Native nextcounter +50=-1 is separate from its displayed image +44.
  * event.parameter stores the NEXT frame, keeping compact displayed frame valid. */
 o->state.monster.state=1;o->state.monster.course_counter=(int16_t)field(a,id,0xae);o->state.monster.course_counter_known=1;
 uint16_t index=a->world->level->definitions[id].actor_index;
 a->last[index]=a->now;dok_scheduler_owner(&a->scheduler,index);
 if(!dok_scheduler_schedule(&a->scheduler,index,1,0,416)){++a->errors;return 0;}
 o->state.monster.burst_started=a->now;o->state.monster.shot_count=0;
 ++a->wakes;emit(a,DOK_AI_WAKE,id);return 1;
}
void dok_ai_consider(dok_ai *a,uint16_t id){
 if(!a || !a->world || !a->world->player_index || id>=a->world->object_count || a->world->level->definitions[id].class_id!=9)return;
 dok_runtime_actor *m=actor(a,id),*p=actor(a,a->world->player_index-1);
 if(m && p && dok_ai_near(p->x-m->x,p->z-m->z,field(a,id,0xba)))dok_ai_wake(a,id);
}
static unsigned cell(dok_ai *a,int32_t x,int32_t z){
 const dok_runtime_level *l=a->world->level;int side=l->settings[DOK_LEVEL_CELL_SIDE],w=l->settings[DOK_LEVEL_WIDTH],h=l->settings[DOK_LEVEL_LENGTH];
 int cx=x/side,cz=z/side;if(cx<0)cx=0;if(cx>=w)cx=w-1;if(cz<0)cz=0;if(cz>=h)cz=h-1;return (unsigned)(cz*w+cx);
}
static unsigned lookup(dok_ai *a,dok_runtime_actor *p,unsigned plane,int side){
 dok_motion_free(p,(int16_t)-side,0,0);
 unsigned pos=cell(a,p->x,p->z);
 if(!plane && a->combat && a->combat->world==a->world){
  /* Native lookup clamps both static and dynamic MMap to the same cell. */
  unsigned width=a->world->level->settings[DOK_LEVEL_WIDTH],cell_side=a->world->level->settings[DOK_LEVEL_CELL_SIDE];
  unsigned dynamic=dok_combat_object_at(a->combat,(int32_t)((pos%width)*cell_side+cell_side/2),(int32_t)((pos/width)*cell_side+cell_side/2));if(dynamic!=DOK_RUNTIME_NONE)return dynamic;
 }
 unsigned glyph=a->world->maps[plane*a->world->level->cells+pos];
 unsigned token=a->world->level->glyph_index[glyph];return token?token-1:DOK_RUNTIME_NONE;
}
void dok_ai_connect_combat(dok_ai *a,dok_combat *c){a->combat=c;}
void dok_ai_notice(dok_ai *a,unsigned kind,uint16_t id){
 if(!a || !a->world || id>=a->world->object_count || a->world->level->definitions[id].class_id!=9)return;
 if(kind==DOK_COMBAT_WAKE)dok_ai_wake(a,id);
 else if(kind==DOK_COMBAT_RECLAIM)dok_ai_remove(a,id);
 else if(kind==DOK_COMBAT_DAMAGE){a->world->objects[id].state.monster.state=3;++a->hurts;emit(a,DOK_AI_HURT,id);}
}
static int view(dok_ai *a,uint16_t id,unsigned slot,uint16_t *resource,dok_animation_view *av){
 dok_rom_resource r;
 return dok_rom_binding(resource,a->assets,a->level,a->language,id,(uint16_t)slot)==DOK_ROM_OK && dok_rom_resource_at(&r,a->assets,*resource,a->language)==DOK_ROM_OK && dok_animation_open(r.data,r.size,av)==DOK_ASSET_OK && av->frame_count;
}
static int show(dok_ai *a,uint16_t id,unsigned slot,unsigned frame){
 uint16_t resource;dok_animation_view av;dok_animation_frame f;
 if(!view(a,id,slot,&resource,&av) || dok_animation_frame_at(&av,(uint16_t)frame,&f)!=DOK_ASSET_OK){++a->errors;return 0;}
 dok_runtime_object *o=&a->world->objects[id];o->frame=(uint16_t)frame;o->visual_resource=resource;o->interval=f.interval_bits;o->animation_a=f.parameter_a;o->animation_b=f.parameter_b;o->known|=7;
 dok_scheduler_interval(&a->scheduler,(uint32_t)(int32_t)f.interval_ticks);return 1;
}
static int sight(dok_ai *a,uint16_t id,uint16_t target){
 dok_runtime *w=a->world;dok_runtime_actor probe=*actor(a,id);
 unsigned pos=cell(a,probe.x,probe.z);uint8_t saved=w->maps[pos];w->maps[pos]=' ';int result=0;
 for(unsigned i=0;i<5000;i++){
  unsigned front=lookup(a,&probe,0,100);
  if(front==id)continue;
  if(front==target){result=1;break;}
  if(front==DOK_RUNTIME_NONE || !(w->objects[front].flags&0x18))break;
  front=lookup(a,&probe,1,100);
  if(front==DOK_RUNTIME_NONE || !(w->objects[front].flags&0x18))break;
 }w->maps[pos]=saved;return result;
}
static void dispatch(dok_scheduler *s,uint16_t index,uint16_t cb,uint32_t parameter,void *context){
 (void)cb;dok_ai *a=context;dok_runtime *w=a->world;
 unsigned token=w->level->glyph_index[w->actors[index].glyph];if(!token){dok_scheduler_cancel(s);return;}uint16_t id=(uint16_t)(token-1);
 dok_runtime_object *o=&w->objects[id];dok_runtime_actor *m=actor(a,id);
 if(!m || (o->flags&0x400)){dok_scheduler_cancel(s);return;}
 unsigned next=parameter;dok_runtime_monster *monster=&o->state.monster;
 if(monster->health<=0){
  if(monster->state!=4){monster->state=4;next=0;++a->deaths;emit(a,DOK_AI_DYING,id);}
  uint16_t resource;dok_animation_view av;
  if(!view(a,id,0xec,&resource,&av)){++a->errors;dok_scheduler_cancel(s);return;}
  if(next>=av.frame_count){
   unsigned pos=cell(a,m->x,m->z);w->maps[pos]=' ';
   unsigned token=w->level->glyph_index[w->maps[w->level->cells+pos]];
   unsigned plane=token && !w->level->definitions[token-1].class_id?1:0;
   w->maps[plane*w->level->cells+pos]=m->glyph;monster->corpse=1;o->flags|=0x10;
   ++a->completed;emit(a,DOK_AI_DEAD,id);dok_scheduler_destroy(s,index);return;
  }
  if(show(a,id,0xec,next))a->owners[index].events[0].parameter=next+1;else dok_scheduler_cancel(s);return;
 }
 int side=w->level->settings[DOK_LEVEL_CELL_SIDE];dok_runtime_actor probe=*m;
 unsigned front=lookup(a,&probe,0,side);
 if(front!=DOK_RUNTIME_NONE && !w->level->definitions[front].class_id)front=lookup(a,&probe,1,side);
 if(front!=DOK_RUNTIME_NONE && w->level->definitions[front].class_id==1){
  ++a->contacts;emit(a,DOK_AI_CONTACT,id);
  if(monster->state==1){next=0;monster->state=2;++a->attacks;emit(a,DOK_AI_ATTACK,id);}
  uint16_t resource;dok_animation_view av;
  if(!view(a,id,0xe8,&resource,&av)){++a->errors;dok_scheduler_cancel(s);return;}
  if(next>=av.frame_count){
   ++a->melee;emit(a,DOK_AI_MELEE,id);if(a->combat)dok_combat_damage(a->combat,(uint16_t)front,id,5);next=0;
  }
  if(show(a,id,0xe8,next))a->owners[index].events[0].parameter=next+1;else dok_scheduler_cancel(s);
  if(monster->state==3){show(a,id,0xec,0);monster->state=2;}
  return;
 }

 dok_runtime_actor *p=actor(a,w->player_index-1);if(!p){++a->errors;dok_scheduler_cancel(s);return;}
 o->state.monster.state=1;
 int16_t saved_heading=m->heading;m->heading=dok_ai_heading(p->x-m->x,p->z-m->z);
 if((a->now-monster->burst_started)/10000u>(uint32_t)(int32_t)field(a,id,0xa8)){
  unsigned cls=front==DOK_RUNTIME_NONE?0:w->level->definitions[front].class_id;
  if(monster->shot_count<field(a,id,0xa6) && cls!=9 && cls!=8 && sight(a,id,(uint16_t)(w->player_index-1))){
   ++monster->shot_count;monster->course_counter=(int16_t)field(a,id,0xae);
   if(a->combat){a->combat->now=a->now;dok_combat_spawn(a->combat,id,(uint8_t)field(a,id,0xe7));}
   ++a->ranged;emit(a,DOK_AI_RANGED,id);
  }else{monster->burst_started=a->now;monster->shot_count=0;}
 }
 m->heading=saved_heading;
 if(o->state.monster.course_counter==(int16_t)field(a,id,0xae)){
  o->state.monster.course_counter=0;probe=*m;probe.heading=dok_ai_heading(p->x-m->x,p->z-m->z);
  unsigned old=cell(a,m->x,m->z);uint8_t saved=w->maps[old];if(saved==m->glyph)w->maps[old]=' ';
  for(unsigned i=0;i<10;i++){
   dok_runtime_actor next=probe;dok_motion_free(&next,(int16_t)(-side/2),0,0);
   dok_collision_resolve(w,&probe.x,&probe.z,next.x,next.z);if(cell(a,probe.x,probe.z)!=old)break;
  }
  w->maps[old]=saved;
  m->heading=dok_ai_heading(probe.x-m->x,probe.z-m->z);++a->courses;emit(a,DOK_AI_COURSE,id);
 }else o->state.monster.course_counter=(int16_t)((uint16_t)o->state.monster.course_counter+1u);
 uint16_t resource=0;dok_rom_resource r;dok_animation_view av;dok_animation_frame f;
 if(dok_rom_binding(&resource,a->assets,a->level,a->language,id,DOK_ROM_SLOT_ANIMATION)!=DOK_ROM_OK || dok_rom_resource_at(&r,a->assets,resource,a->language)!=DOK_ROM_OK || dok_animation_open(r.data,r.size,&av)!=DOK_ASSET_OK || !av.frame_count){++a->errors;dok_scheduler_cancel(s);return;}
 unsigned frame=parameter;if(frame>=av.frame_count)frame=0;
 if(dok_animation_frame_at(&av,(uint16_t)frame,&f)!=DOK_ASSET_OK){++a->errors;dok_scheduler_cancel(s);return;}
 o->frame=(uint16_t)frame;o->visual_resource=resource;o->interval=f.interval_bits;o->animation_a=f.parameter_a;o->animation_b=f.parameter_b;
 o->known|=DOK_RUNTIME_INTERVAL|DOK_RUNTIME_ANIMATION_A|DOK_RUNTIME_ANIMATION_B;
 a->owners[index].events[0].parameter=frame+1;
 unsigned ai=w->level->definitions[id].actor_index;uint32_t dt=a->now-a->last[index];a->last[index]=a->now;
 if(!dok_motion_original(m,&a->motion[ai],DOK_INPUT_FORWARD,dt,dok_collision_move,w)){++a->blocked;emit(a,DOK_AI_BLOCKED,id);}
 ++a->steps;dok_scheduler_interval(s,(uint32_t)(int32_t)f.interval_ticks);
}
void dok_ai_poll(dok_ai *a,uint32_t tick){if(!a || !a->world)return;a->now=tick;dok_scheduler_dispatch(&a->scheduler,dispatch,a);}
