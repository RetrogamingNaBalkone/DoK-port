/* G11 real-ROM animation contract: every default/attack/death binding. */
#include "dok/visual.h"
#include "dok/animation.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static dok_visual visual;static dok_runtime world;static dok_runtime_level view;
static unsigned events,ends;
static dok_runtime_object timeline_objects[DOK_RUNTIME_MAX_OBJECTS];
static dok_event_owner timeline_owners[DOK_RUNTIME_MAX_OBJECTS];
static uint8_t visible[DOK_RUNTIME_MAX_OBJECTS];
static void notice(void *p,uint16_t id,uint16_t frame,int complete) {(void)p;(void)id;(void)frame;if(complete) ++ends;else ++events;}
int main(int argc,char **argv) {
 if(argc!=2) return 2;FILE *file=fopen(argv[1],"rb");if(!file) return 2;
 fseek(file,0,SEEK_END);long length=ftell(file);rewind(file);void *bytes=malloc((size_t)length);
 if(!bytes || fread(bytes,1,(size_t)length,file)!=(size_t)length) return 2;fclose(file);
 dok_rom_assets a;if(dok_rom_open(&a,bytes,(size_t)length)) return 3;
 unsigned sequences=0,frames=0,negative=0;
 for(unsigned level=0;level<3;++level) for(unsigned lang=0;lang<2;++lang) {
  if(dok_rom_level(&view,&a,level,lang) || dok_runtime_reset(&world,&view)) return 4;
  for(uint16_t id=0;id<world.object_count;++id) for(unsigned slot=0;slot<=view.definitions[id].field_count;++slot) {
   const dok_runtime_field *field=slot?&view.fields[view.definitions[id].field_begin+slot-1]:0;
   if(field && field->kind!=DOK_LEVEL_ANIMATION) continue;
   uint16_t binding=field?field->native_offset:DOK_ROM_SLOT_ANIMATION;
   uint16_t resource;if(dok_rom_binding(&resource,&a,level,lang,id,binding)) continue;
   dok_rom_resource res;dok_animation_view av;dok_animation_frame f;
   if(dok_rom_resource_at(&res,&a,resource,lang) || dok_animation_open(res.data,res.size,&av)) return 5;
   dok_runtime_player player=world.player;dok_runtime_monster monster=world.objects[id].state.monster;
   dok_visual_init(&visual,&world,&a,level,lang,0,notice,0);unsigned before=ends;
   if(!dok_visual_play(&visual,id,binding,0)) return 6;
   for(uint16_t n=0;n<av.frame_count;++n) {
    if(dok_animation_frame_at(&av,n,&f) || world.objects[id].frame!=n || world.objects[id].interval!=f.interval_bits ||
       world.objects[id].animation_a!=f.parameter_a || world.objects[id].animation_b!=f.parameter_b) return 7;
    uint32_t deadline=visual.owners[id].events[0].deadline;
    if(deadline>visual.now) {dok_visual_poll(&visual,deadline-1);if(world.objects[id].frame!=n) return 8;}
    if(f.interval_ticks<0) ++negative;
    dok_visual_poll(&visual,deadline);++frames;
   }
   if(ends!=before+1 || visual.completed!=1 || visual.errors || visual.owners[id].count ||
      memcmp(&player,&world.player,sizeof(player)) || memcmp(&monster,&world.objects[id].state.monster,sizeof(monster))) return 9;
   ++sequences;
   /* Loop end repeats frame0, lease does not read beyond final frame. */
   dok_visual_init(&visual,&world,&a,level,lang,0,notice,0);
   if(!dok_visual_play(&visual,id,binding,1)) return 10;
   for(unsigned n=0;n<av.frame_count;++n) dok_visual_poll(&visual,visual.owners[id].events[0].deadline);
   if(world.objects[id].frame || visual.completed || visual.errors) return 11;
  }
 }
 /* Native effect callback configures crop with the OLD counter, then INC.
  * Current crop survives END/cancel/rearm until the next callback. */
 if(dok_rom_level(&view,&a,1,0) || dok_runtime_reset(&world,&view)) return 16;
 dok_visual_init(&visual,&world,&a,1,0,0,0,0);
 uint16_t water=0;while(water<world.object_count && view.definitions[water].class_id!=12) ++water;
 if(water==world.object_count) return 17;
 dok_visual_touch(&visual,water);
 for(unsigned crop=0;crop<18;++crop) {
  dok_visual_poll(&visual,0);
  dok_runtime_object *o=&world.objects[water];
  if(o->frame!=crop+1 || o->animation_a!=(int)crop || !(o->known&DOK_RUNTIME_CROP_PHASE) || (o->known&DOK_RUNTIME_ANIMATION_A)) return 18;
 }
 dok_visual_poll(&visual,0);
 if(visual.owners[water].count || world.objects[water].animation_a!=17) return 19;
 dok_visual_touch(&visual,water);
 if(world.objects[water].frame || world.objects[water].animation_a!=17) return 20;
 dok_visual_poll(&visual,0);
 if(world.objects[water].frame!=1 || world.objects[water].animation_a) return 21;
 /* Exercise the actual production timeline with coarse and fine wall-time
  * delivery. All quanta, zero-interval water and lease renewal stay identical. */
 for(unsigned level=0;level<3;++level) {
  if(dok_rom_level(&view,&a,level,0)) return 12;
  for(unsigned mode=0;mode<3;++mode) {
   if(dok_runtime_reset(&world,&view)) return 13;
   dok_visual_init(&visual,&world,&a,level,0,0,0,0);
   for(unsigned id=0;id<world.object_count;++id) visible[id]=1;
   uint32_t chunk=mode==0?10:(mode==1?416:5000);
   for(uint32_t tick=chunk;tick<300000;tick+=chunk) dok_visual_advance(&visual,tick,visible);
   dok_visual_advance(&visual,300000,visible);
   if(visual.errors || visual.now!=300000) return 14;
   if(!mode) {memcpy(timeline_objects,world.objects,sizeof(timeline_objects));memcpy(timeline_owners,visual.owners,sizeof(timeline_owners));}
   else if(memcmp(timeline_objects,world.objects,sizeof(timeline_objects)) || memcmp(timeline_owners,visual.owners,sizeof(timeline_owners))) return 15;
  }
 }
 printf("{\"sequences\":%u,\"frame_deadlines\":%u,\"negative_intervals\":%u,\"frame_events\":%u,\"completion_events\":%u,\"water_crop_then_increment\":true,\"coarse_fine_timelines\":9,\"status\":\"passed\"}\n",sequences,frames,negative,events,ends);
 free(bytes);return 0;
}
