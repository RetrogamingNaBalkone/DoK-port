#include "dok/interactions.h"
#include "dok/collision.h"
#include "dok/movement.h"

enum { OPEN=1,CLOSE,AUTO };
static uint32_t now(void *p) {return ((dok_interactions *)p)->now;}
static int32_t field(const dok_runtime_level *l,unsigned id,unsigned offset)
{
 const dok_runtime_definition *d=&l->definitions[id];
 for(unsigned i=0;i<d->field_count;i++) {
  const dok_runtime_field *f=&l->fields[d->field_begin+i];
  if(f->native_offset==offset && f->kind<=DOK_LEVEL_CHAR) return f->value.number;
 }
 return 0;
}
static void emit(dok_interactions *s,dok_interaction_kind kind,uint16_t id,int32_t a,int32_t b)
{if(s->sink) s->sink(s->context,kind,id,a,b);}
static void schedule(dok_interactions *s,uint16_t id,unsigned callback,uint32_t interval)
{if(!dok_scheduler_schedule(&s->scheduler,id,(uint16_t)callback,0,interval)) ++s->errors;}
static void dispatch(dok_scheduler *scheduler,uint16_t id,uint16_t callback,uint32_t parameter,void *p)
{
 (void)parameter;dok_interactions *s=p;dok_runtime_object *o=&s->world->objects[id];
 dok_runtime_door *d=&o->state.door;
 if(callback==OPEN) {
  if(d->parameter_54==2 && (int16_t)o->frame<d->extent-1) {
   o->flags|=8;d->parameter_55=0;dok_scheduler_interval(scheduler,(uint32_t)(int32_t)(int16_t)o->interval);
   ++o->frame;
   if((int16_t)o->frame>d->extent/4) o->flags|=0x10;
  } else {
   d->parameter_54=1;o->flags|=0x10;d->parameter_55=1;
   emit(s,DOK_INTERACTION_OPENED,id,0,0);dok_scheduler_cancel(scheduler);
  }
 } else if(callback==CLOSE) {
  if(d->state_56<1) {
   if(d->parameter_54==1 && (int16_t)o->frame>0) {
    --o->frame;d->parameter_55=0;dok_scheduler_interval(scheduler,(uint32_t)(int32_t)(int16_t)o->interval);
   } else {
    d->parameter_54=2;o->frame=0;o->flags=d->original_flags;d->parameter_55=1;
    emit(s,DOK_INTERACTION_CLOSED,id,0,0);dok_scheduler_cancel(scheduler);
   }
  }
 } else if(callback==AUTO) {
  if(d->state_56<1 && d->parameter_54==1 && d->parameter_55) {
   schedule(s,id,CLOSE,(uint32_t)d->parameter_66);dok_scheduler_cancel(scheduler);
  }
 } else ++s->errors;
}
void dok_interactions_init(dok_interactions *s,dok_runtime *w,uint32_t tick,dok_interaction_sink sink,void *context)
{
 s->world=w;s->now=tick;s->visual_steps=0;s->previous_zone=DOK_RUNTIME_NONE;s->errors=0;
 s->sink=sink;s->context=context;
 dok_scheduler_init(&s->scheduler,s->owners,DOK_RUNTIME_MAX_OBJECTS,now,s);
 if(w && w->level) for(uint16_t i=0;i<w->object_count;i++) dok_scheduler_owner(&s->scheduler,i);
}
void dok_interactions_poll(dok_interactions *s,uint32_t tick)
{s->now=tick;++s->visual_steps;dok_scheduler_dispatch(&s->scheduler,dispatch,s);}
uint32_t dok_interactions_event(dok_interactions *s,uint16_t id,uint32_t event,uint16_t source)
{
 dok_runtime *w=s->world;if(!w || !w->level || id>=w->object_count || source>=w->object_count) return event;
 dok_runtime_object *o=&w->objects[id];unsigned cls=w->level->definitions[id].class_id;
 if(cls!=7) {
  /* Native class6 inherits base event method. No invented toggle behaviour. */
  if((cls!=12 && cls!=13) && !(o->flags&0x18)) event&=~0x40000u;
  return event;
 }
 dok_runtime_door *d=&o->state.door;
 if(!(o->flags&8)) event&=~0x40000u;
 if((event&DOK_INTERACTION_USE) && d->parameter_55) {
  d->extent=DOK_INTERACTION_FPS;event&=~(uint32_t)DOK_INTERACTION_USE;
  unsigned keys=w->level->definitions[source].class_id==13?65535u:w->player.keys;
  if(d->parameter_5c && !((uint16_t)d->parameter_5c&keys)) {
   emit(s,DOK_INTERACTION_LOCKED,id,d->parameter_5c,0);return event;
  }
  if(d->parameter_54==2) {
   schedule(s,id,OPEN,416);
   if(d->parameter_5e==1) schedule(s,id,AUTO,(uint32_t)d->parameter_66);
  } else if(d->parameter_5e==0) schedule(s,id,CLOSE,416);
 }
 return event;
}
static unsigned at(const dok_runtime *w,int32_t x,int32_t z)
{
 int32_t side=w->level->settings[DOK_LEVEL_CELL_SIDE];
 int col=x/side,row=z/side,width=w->level->settings[DOK_LEVEL_WIDTH],length=w->level->settings[DOK_LEVEL_LENGTH];
 if(col<0) col=0;if(col>=width) col=width-1;
 if(row<0) row=0;if(row>=length) row=length-1;
 return (unsigned)(row*width+col);
}
void dok_interactions_use(dok_interactions *s,const dok_runtime_actor *a)
{
 dok_runtime *w=s->world;if(!w || !w->level || !a || !w->player_index) return;
 /* Native player makes a one-cell forward probe (negative local distance).
  * Reuse verified Q15 trig and SAR arithmetic without quarter-step rounding. */
 dok_runtime_actor probe=*a;
 dok_motion_free(&probe,-(int16_t)w->level->settings[DOK_LEVEL_CELL_SIDE],0,0);
 int32_t x=probe.x,z=probe.z;
 unsigned glyph=w->maps[w->level->cells+at(w,x,z)],id=w->level->glyph_index[glyph];
 if(id) dok_interactions_event(s,(uint16_t)(id-1),DOK_INTERACTION_USE,(uint16_t)(w->player_index-1));
}
void dok_interactions_zone(dok_interactions *s,dok_runtime_actor *a)
{
 dok_runtime *w=s->world;if(!w || !w->level || !a) return;
 unsigned cell=at(w,a->x,a->z),glyph=w->maps[2*w->level->cells+cell],token=w->level->glyph_index[glyph];
 if(!token || token>w->object_count) {s->previous_zone=DOK_RUNTIME_NONE;return;}
 uint16_t id=(uint16_t)(token-1);unsigned cls=w->level->definitions[id].class_id;
 if(cls==13 && s->previous_zone!=id) {
  unsigned target=w->level->glyph_index[(uint8_t)field(w->level,id,0x66)];
  if(target) dok_interactions_event(s,(uint16_t)(target-1),(uint32_t)field(w->level,id,0x67),id);
  else {++s->errors;emit(s,DOK_INTERACTION_UNSUPPORTED,id,0,0);}
 } else if(cls==12) {
  int32_t category=field(w->level,id,0x66),value=field(w->level,id,0x68)/DOK_INTERACTION_FPS;
  a->motion_flags|=0x4000000u;
  if(category==1000) {
   /* G18 consumes the condition; do not emulate death/menu as a side effect. */
   if(w->player.score==w->pickup_99_total) emit(s,DOK_INTERACTION_FINISH,id,category,value);
  } else if(category==100 || category==101 || category==102) {
   dok_collision_displace(w,a,(int16_t)(category==102?0:value),(int16_t)(category==102?value:0));
  } else emit(s,DOK_INTERACTION_STAT,id,category,value);
 }
 s->previous_zone=id;
}
