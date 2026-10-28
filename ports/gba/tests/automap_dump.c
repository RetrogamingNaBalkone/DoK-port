/* Fixture runner for independent DOS instruction comparison. */
#include "dok/ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static dok_runtime world;
static dok_runtime_level level;
static dok_runtime_definition definitions[256];
static unsigned read16(FILE *f){unsigned a=(unsigned)fgetc(f),b=(unsigned)fgetc(f);return a|(b<<8);}
int main(int argc,char **argv){
 if(argc!=3 && argc!=4)return 1;FILE *f=fopen(argv[1],"rb");if(!f)return 2;
 level.settings[DOK_LEVEL_WIDTH]=(int16_t)read16(f);level.settings[DOK_LEVEL_LENGTH]=(int16_t)read16(f);
 level.settings[DOK_LEVEL_CELL_SIDE]=480;level.cells=4096;level.object_count=(uint16_t)read16(f);
 world.level=&level;level.definitions=definitions;world.object_count=level.object_count;world.player_index=(uint16_t)read16(f);
 world.actor_count=1;world.player.map_revealed=(uint16_t)read16(f);
 world.actors[0].x=(int16_t)read16(f)*480+240;world.actors[0].z=(int16_t)read16(f)*480+240;
 world.actors[0].heading=(int16_t)read16(f);
 for(unsigned i=0;i<level.object_count;++i){
  dok_runtime_definition *d=&definitions[i];d->glyph=(uint8_t)read16(f);d->class_id=(uint16_t)read16(f);d->marker=(uint8_t)read16(f);
  d->actor_index=d->class_id==1?0:DOK_RUNTIME_NONE;world.objects[i].state.monster.state=(int16_t)read16(f);
  level.glyph_index[d->glyph]=(uint16_t)(i+1);
 }
 if(fread(world.maps,1,16384,f)!=16384)return 3;fclose(f);
 dok_ui u;dok_ui_init(&u);u.state=DOK_UI_MAP;u.map_centered=1;u.map_x=80;u.map_y=(int16_t)(65+(argc==4?atoi(argv[3]):0));
 uint8_t frame[38400],font8[2048]={0},font16[4096]={0};
 dok_ui_draw(&u,frame,&world,font8,font16,255,0);
 f=fopen(argv[2],"wb");if(!f)return 4;fwrite(frame,1,sizeof(frame),f);fclose(f);return 0;
}
