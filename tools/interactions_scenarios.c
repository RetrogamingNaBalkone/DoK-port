#include "dok/interactions.h"
#include "dok/rom_assets.h"
#include <stdio.h>
#include <stdlib.h>
static dok_runtime w;
static dok_interactions s;
#define CHECK(x) do {if(!(x)) {fprintf(stderr,"Scenario line %u level%u object%u\n",__LINE__,level,id);return 1;}} while(0)
int main(int argc,char **argv)
{
 if(argc!=2) return 2;
 FILE *f=fopen(argv[1],"rb");if(!f || fseek(f,0,SEEK_END)) return 3;
 long size=ftell(f);if(size<0 || fseek(f,0,SEEK_SET)) return 3;
 unsigned char *bytes=malloc((size_t)size);if(!bytes || fread(bytes,1,(size_t)size,f)!=(size_t)size) return 3;
 fclose(f);dok_rom_assets assets;if(dok_rom_open(&assets,bytes,(size_t)size)) return 4;
 unsigned doors=0,switches=0,sensors=0,zones=0;
 for(unsigned language=0;language<2;language++) for(unsigned level=0;level<3;level++) {
  dok_runtime_level l;if(dok_rom_level(&l,&assets,level,language)) return 5;
  for(unsigned id=0;id<l.object_count;id++) {
   CHECK(!dok_runtime_reset(&w,&l));dok_interactions_init(&s,&w,0,0,0);
   unsigned cls=l.definitions[id].class_id;
   if(cls==7) {
    dok_runtime_object *o=&w.objects[id];dok_runtime_door *d=&o->state.door;
    w.player.keys=0;dok_interactions_event(&s,(uint16_t)id,0x800,(uint16_t)(l.player_index-1));
    if(d->parameter_5c) {CHECK(!s.owners[id].count);w.player.keys=(uint16_t)d->parameter_5c;dok_interactions_event(&s,(uint16_t)id,0x800,(uint16_t)(l.player_index-1));}
    CHECK(s.owners[id].count>0);
    for(unsigned n=1;n<=40;n++) dok_interactions_poll(&s,n*10000/30);
    CHECK(d->parameter_54==1 && d->parameter_55 && (o->flags&16));
    if(d->parameter_5e==0) dok_interactions_event(&s,(uint16_t)id,0x800,(uint16_t)(l.player_index-1));
    else {d->state_56=1;for(unsigned n=41;n<=400;n++) dok_interactions_poll(&s,n*10000/30);CHECK(d->parameter_54==1);d->state_56=0;}
    for(unsigned n=401;n<=900;n++) dok_interactions_poll(&s,n*10000/30);
    CHECK(d->parameter_54==2 && d->parameter_55 && o->frame==0 && o->flags==d->original_flags && !s.errors);doors++;
   } else if(cls==6) {CHECK(dok_interactions_event(&s,(uint16_t)id,0x800,(uint16_t)(l.player_index-1))==0x800);CHECK(!s.owners[id].count);switches++;}
   else if(cls==12 || cls==13) {
    dok_runtime_actor *a=&w.actors[l.definitions[l.player_index-1].actor_index];
    /* Sample every actual zone cell with clean self-occupancy. */
    for(unsigned c=0;c<l.cells;c++) if(w.maps[2*l.cells+c]==l.definitions[id].glyph) {
     for(unsigned m=0;m<l.cells;m++) if(w.maps[m]==a->glyph) w.maps[m]=' ';
     a->x=(int32_t)(c%64)*480+240;a->z=(int32_t)(c/64)*480+240;w.maps[c]=a->glyph;
     s.previous_zone=DOK_RUNTIME_NONE;dok_interactions_zone(&s,a);CHECK(!s.errors);
     if(cls==13) {sensors++;for(unsigned n=1;n<=40;n++) dok_interactions_poll(&s,s.now+334);}
     else zones++;
    }
   }
  }
 }
 printf("{\"status\":\"passed\",\"door_definitions\":%u,\"class6_definitions\":%u,\"sensor_cells\":%u,\"effect_cells\":%u}\n",doors,switches,sensors,zones);
 free(bytes);return 0;
}
