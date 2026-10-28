#include "dok/flow.h"
int dok_flow_retry(dok_runtime *w){
 if(!w || !w->level || !w->player_index)return 0;
 unsigned id=w->player_index-1,index=w->level->definitions[id].actor_index;if(index>=w->actor_count)return 0;
 dok_runtime_actor *a=&w->actors[index];const dok_runtime_actor *initial=&w->level->initial_actors[index];
 int side=w->level->settings[DOK_LEVEL_CELL_SIDE],width=w->level->settings[DOK_LEVEL_WIDTH],height=w->level->settings[DOK_LEVEL_LENGTH];
 int x=a->x/side,z=a->z/side;if(x<0)x=0;if(x>=width)x=width-1;if(z<0)z=0;if(z>=height)z=height-1;
 w->maps[z*width+x]=' ';a->x=initial->x;a->y=initial->y;a->z=initial->z;
 a->heading=initial->heading;a->angle_34=initial->angle_34;a->mode=initial->mode;
 a->base_speed=initial->base_speed;a->step_distance=initial->step_distance;
 x=a->x/side;z=a->z/side;
 if(x>=0 && z>=0 && x<width && z<height)w->maps[z*width+x]=a->glyph;
 w->player.health=w->player.health_max;return 1;
}
