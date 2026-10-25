#include "dok/interactions.h"
#include <stdio.h>
static dok_runtime w;
static dok_interactions s;
int main(void)
{
 int op,flags,frame,phase,ready,occupancy,extent,lock,mode,delay,interval,event,keys,source;
 dok_runtime_definition defs[2]={{0}};dok_runtime_level l={0};l.definitions=defs;l.object_count=2;
 defs[0].class_id=7;w.level=&l;w.object_count=2;
 while(scanf("%d%d%d%d%d%d%d%d%d%d%d%d%d%d",&op,&flags,&frame,&phase,&ready,&occupancy,&extent,&lock,&mode,&delay,&interval,&event,&keys,&source)==14) {
  w.objects[0]=(dok_runtime_object){0};dok_runtime_object *o=&w.objects[0];dok_runtime_door *d=&o->state.door;
  o->flags=(uint16_t)flags;o->frame=(uint16_t)frame;o->interval=(uint16_t)interval;
  d->parameter_54=(uint8_t)phase;d->parameter_55=(uint8_t)ready;d->state_56=(int16_t)occupancy;
  d->extent=(int16_t)extent;d->parameter_5c=(int16_t)lock;d->parameter_5e=(int16_t)mode;d->parameter_66=delay;d->original_flags=64;
  w.player.keys=(uint16_t)keys;defs[1].class_id=(uint16_t)source;
  dok_interactions_init(&s,&w,0,0,0);
  defs[0].class_id=op==4?6:7;
  uint32_t remaining=(uint32_t)event;
  if(!op || op==4) remaining=dok_interactions_event(&s,0,remaining,1);
  else {dok_scheduler_schedule(&s.scheduler,0,(uint16_t)op,0,0);dok_interactions_poll(&s,0);}
  printf("%u %d %u %u %d %d %d %u %u",o->flags,(int16_t)o->frame,d->parameter_54,d->parameter_55,d->state_56,d->extent,(int16_t)o->interval,remaining,s.owners[0].count);
  for(unsigned i=0;i<3;i++) {dok_event *e=&s.owners[0].events[i];printf(" %u %u",i<s.owners[0].count?e->callback:0,i<s.owners[0].count?e->interval:0);}
  puts("");
 }
 return 0;
}
