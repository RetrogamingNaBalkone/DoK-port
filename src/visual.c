#include "dok/visual.h"
#include "dok/animation.h"
static uint32_t now(void *p) {return ((dok_visual *)p)->now;}
static int frame(dok_visual *v,uint16_t id,uint16_t index,dok_animation_frame *f,uint16_t *count) {
 dok_rom_resource res;dok_animation_view a;
 if(dok_rom_resource_at(&res,v->assets,v->resource[id],v->language)!=DOK_ROM_OK ||
    dok_animation_open(res.data,res.size,&a)!=DOK_ASSET_OK) return 0;
 *count=a.frame_count;return dok_animation_frame_at(&a,index,f)==DOK_ASSET_OK;
}
static void publish(dok_visual *v,uint16_t id,const dok_animation_frame *f) {
 dok_runtime_object *o=&v->world->objects[id];
 o->visual_resource=v->resource[id];o->frame=f->index;o->interval=f->interval_bits;o->animation_a=f->parameter_a;o->animation_b=f->parameter_b;
 o->known|=DOK_RUNTIME_INTERVAL|DOK_RUNTIME_ANIMATION_A|DOK_RUNTIME_ANIMATION_B;
 if(v->sink) v->sink(v->context,id,f->index,0);
}
static void dispatch(dok_scheduler *s,uint16_t id,uint16_t cb,uint32_t parameter,void *p) {
 (void)parameter;dok_visual *v=p;dok_runtime_object *o=&v->world->objects[id];
 if(cb==2) {
  if(o->frame<18) {
   o->animation_a=(int16_t)o->frame;o->known|=DOK_RUNTIME_CROP_PHASE;
   ++o->frame;++v->steps;dok_scheduler_interval(s,(uint32_t)(int32_t)(int16_t)o->interval);
  }
  else dok_scheduler_cancel(s);
  return;
 }
 if(v->loop[id] && !o->animation_budget) {dok_scheduler_cancel(s);return;}
 if(v->loop[id]) --o->animation_budget;
 dok_animation_frame f;uint16_t count;unsigned next=(unsigned)o->frame+1;
 if(!frame(v,id,0,&f,&count)) {++v->errors;dok_scheduler_cancel(s);return;}
 if(next>=count) {
  if(!v->loop[id]) {++v->completed;dok_scheduler_cancel(s);
   if(v->sink) v->sink(v->context,id,o->frame,1);return;}
  next=0;
 }
 if(!frame(v,id,(uint16_t)next,&f,&count)) {++v->errors;dok_scheduler_cancel(s);return;}
 publish(v,id,&f);++v->steps;
 dok_scheduler_interval(s,(uint32_t)(int32_t)f.interval_ticks);
}
void dok_visual_init(dok_visual *v,dok_runtime *w,const dok_rom_assets *a,unsigned level,unsigned language,uint32_t tick,dok_visual_sink sink,void *context) {
 v->world=w;v->assets=a;v->level=level;v->language=language;v->now=tick;
 v->steps=v->completed=v->errors=0;v->sink=sink;v->context=context;
 v->next=tick+333;v->phase=0;
 dok_scheduler_init(&v->scheduler,v->owners,DOK_RUNTIME_MAX_OBJECTS,now,v);
 for(unsigned i=0;i<DOK_RUNTIME_MAX_OBJECTS;++i) {v->resource[i]=0;v->loop[i]=0;
  if(i<w->object_count) dok_scheduler_owner(&v->scheduler,(uint16_t)i);}
}
int dok_visual_play(dok_visual *v,uint16_t id,uint16_t slot,int loop) {
 if(!v || !v->world || id>=v->world->object_count) return 0;
 uint16_t res,count;dok_animation_frame f;
 if(dok_rom_binding(&res,v->assets,v->level,v->language,id,slot)!=DOK_ROM_OK) return 0;
 uint16_t previous=v->resource[id];v->resource[id]=res;
 if(!frame(v,id,0,&f,&count)) {v->resource[id]=previous;return 0;}
 dok_scheduler_destroy(&v->scheduler,id);dok_scheduler_owner(&v->scheduler,id);
 v->loop[id]=(uint8_t)(loop!=0);v->world->objects[id].animation_budget=200;publish(v,id,&f);
 return dok_scheduler_schedule(&v->scheduler,id,1,0,(uint32_t)(int32_t)f.interval_ticks);
}
void dok_visual_touch(dok_visual *v,uint16_t id) {
 if(!v || id>=v->world->object_count) return;
 dok_runtime_object *o=&v->world->objects[id];unsigned cls=v->world->level->definitions[id].class_id;
 if(cls==12 || cls==13) {
  if(o->frame==18 && !v->owners[id].count) {o->frame=0;
   if(!dok_scheduler_schedule(&v->scheduler,id,2,0,(uint32_t)(int32_t)(int16_t)o->interval)) ++v->errors;}
 } else if((o->flags&0x800) && cls!=9 && cls!=8 && !v->owners[id].count) {
  if(v->resource[id]) {
   o->animation_budget=200;
   if(!dok_scheduler_schedule(&v->scheduler,id,1,0,(uint32_t)(int32_t)(int16_t)o->interval)) ++v->errors;
  } else if(!dok_visual_play(v,id,DOK_ROM_SLOT_ANIMATION,1)) ++v->errors;
 }
}
void dok_visual_poll(dok_visual *v,uint32_t tick) {v->now=tick;dok_scheduler_dispatch(&v->scheduler,dispatch,v);}
void dok_visual_advance(dok_visual *v,uint32_t tick,const uint8_t visible[DOK_RUNTIME_MAX_OBJECTS]) {
 for(unsigned n=0;n<128 && (int32_t)(tick-v->next)>=0;++n) {
  dok_visual_poll(v,v->next);
  if(visible) for(uint16_t id=0;id<v->world->object_count;++id) if(visible[id]) dok_visual_touch(v,id);
  v->phase=(v->phase+1)%3;v->next+=v->phase==2?334:333;
 }
}
