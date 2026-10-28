/* DOS PKR 1098:1290 / PKE 1030:1374 (Ghidra NE loader).
 * Original 3px cells, mirrored X, floor colours, exposed wall contours,
 * centre door lines, object squares and heading pixels. Only the viewport,
 * scrolling and input are GBA adaptations. See docs/gba/ui01-dos-map.md. */
static unsigned map_id(const dok_runtime *w,int x,int z,unsigned plane){
 const dok_runtime_level *l=w->level;
 if(x<0 || z<0 || x>=l->settings[DOK_LEVEL_WIDTH] || z>=l->settings[DOK_LEVEL_LENGTH])return DOK_RUNTIME_NONE;
 unsigned token=l->glyph_index[w->maps[plane*l->cells+(unsigned)z*l->settings[DOK_LEVEL_WIDTH]+(unsigned)x]];
 return token && token<=w->object_count?token-1:DOK_RUNTIME_NONE;
}
static unsigned map_class(const dok_runtime *w,int x,int z){
 unsigned id=map_id(w,x,z,1);return id==DOK_RUNTIME_NONE?0:w->level->definitions[id].class_id;
}
static unsigned map_seen(const dok_runtime *w,int x,int z){
 const dok_runtime_level *l=w->level;
 return x>=0 && z>=0 && x<l->settings[DOK_LEVEL_WIDTH] && z<l->settings[DOK_LEVEL_LENGTH] &&
  w->maps[3*l->cells+(unsigned)z*l->settings[DOK_LEVEL_WIDTH]+(unsigned)x];
}
static unsigned map_colour(const dok_runtime *w,unsigned id){
 const dok_runtime_definition *d=&w->level->definitions[id];
 unsigned colour=d->marker;
 if((d->class_id==4 || d->class_id==7) && w->objects[id].state.door.parameter_54==1)colour^=255;
 return colour;
}
static void map_rect(canvas *c,int x,int y,int x2,int y2,unsigned colour){
 if(x<0)x=0;if(y<0)y=0;if(x2>239)x2=239;if(y2>143)y2=143;
 if(x<=x2 && y<=y2)box(c,(unsigned)x,(unsigned)y,(unsigned)(x2-x+1),(unsigned)(y2-y+1),colour);
}
static void map_draw(canvas *c,const dok_runtime *w,dok_ui *u,unsigned en){
 box(c,0,0,240,160,0);
 if(!w->level || !w->player_index)return;
 const dok_runtime_level *l=w->level;int width=l->settings[DOK_LEVEL_WIDTH],height=l->settings[DOK_LEVEL_LENGTH];
 const dok_runtime_actor *a=map_player(w);int side=l->settings[DOK_LEVEL_CELL_SIDE];
 int px=a && side?a->x/side:0,pz=a && side?a->z/side:0;
 if(!u->map_centered){u->map_x=(int16_t)((width-1-px)*3+2);u->map_y=(int16_t)(pz*3+2);u->map_centered=1;}
 if(u->map_x<0)u->map_x=0;if(u->map_x>width*3)u->map_x=(int16_t)(width*3);
 if(u->map_y<0)u->map_y=0;if(u->map_y>height*3)u->map_y=(int16_t)(height*3);
 int ox=120-u->map_x,oy=72-u->map_y;
 unsigned empty=l->glyph_index[32],sky=l->glyph_index[219];
 unsigned bg=empty?l->definitions[empty-1].marker:0,skycolour=sky?l->definitions[sky-1].marker:bg;
 box(c,0,0,240,144,bg);
 /* Floor first, as in DOS; shared inclusive edges are intentional. */
 for(int z=0;z<height;++z)for(int x=0;x<width;++x){
  if(!map_seen(w,x,z))continue;
  unsigned id=map_id(w,x,z,2);if(id==DOK_RUNTIME_NONE)continue;
  unsigned colour=map_colour(w,id);if(colour==bg || colour==skycolour)continue;
  int sx=ox+(width-1-x)*3,sy=oy+z*3;map_rect(c,sx,sy,sx+3,sy+3,colour);
 }
 for(int z=0;z<height;++z)for(int x=0;x<width;++x){
  if(!map_seen(w,x,z) && !(x==px && z==pz))continue;
  unsigned id=map_id(w,x,z,0);if(id==DOK_RUNTIME_NONE)continue;
  unsigned cls=l->definitions[id].class_id;
  if(!cls){id=map_id(w,x,z,1);if(id==DOK_RUNTIME_NONE)continue;cls=l->definitions[id].class_id;}
  unsigned colour=map_colour(w,id);int sx=ox+(width-1-x)*3,sy=oy+z*3;
  if(cls==2){
   if(map_seen(w,x-1,z) && map_class(w,x-1,z)!=2)map_rect(c,sx+3,sy,sx+3,sy+3,colour);
   if(map_seen(w,x,z-1) && map_class(w,x,z-1)!=2)map_rect(c,sx,sy,sx+3,sy,colour);
   if(map_seen(w,x+1,z) && map_class(w,x+1,z)!=2)map_rect(c,sx,sy,sx,sy+3,colour);
   if(map_seen(w,x,z+1) && map_class(w,x,z+1)!=2)map_rect(c,sx,sy+3,sx+3,sy+3,colour);
  }else if(cls==4 || cls==7){
   unsigned left=map_class(w,x-1,z),right=map_class(w,x+1,z);
   if(left!=2 && left!=4 && left!=7 && right!=2 && right!=4 && right!=7)map_rect(c,sx+2,sy+1,sx+2,sy+3,colour);
   unsigned up=map_class(w,x,z-1),down=map_class(w,x,z+1);
   if(up!=2 && up!=4 && up!=7 && down!=2 && down!=4 && down!=7)map_rect(c,sx,sy+1,sx+2,sy+1,colour);
  }else if(cls==8 || cls==12 || (w->player.map_revealed &&
   (cls==3 || cls==10 || cls==6 || cls==13 || (cls==9 && w->objects[id].state.monster.state!=4)))){
   map_rect(c,sx+1,sy+1,sx+2,sy+2,colour);
  }
 }
 if(a && px>=0 && px<width && pz>=0 && pz<height){
  static const uint16_t ink[8]={128,256,32,4,2,1,8,64},erase[8]={18,17,24,80,144,272,48,20};
  unsigned heading=(unsigned)((a->heading%360+360)%360),dir=heading<20 || heading>=341?0:heading<=64?1:heading<=109?2:heading<=154?3:heading<=199?4:heading<=244?5:heading<=289?6:7;
  int sx=ox+(width-1-px)*3+1,sy=oy+pz*3+1;
  unsigned colour=l->definitions[w->player_index-1].marker;
  for(unsigned y=0;y<3;++y)for(unsigned x=0;x<3;++x){unsigned bit=1u<<(y*3+x);
   if((ink[dir]|erase[dir])&bit)map_rect(c,sx+(int)x,sy+(int)y,sx+(int)x,sy+(int)y,ink[dir]&bit?colour:0);
  }
 }
 /* Two short rows; no frame or menu texture over the original plan. */
 label(c,0,144,en?"D-PAD: SCROLL  A: HERO":"D-PAD: \221\204\202\210\203  A: \203\205\220\216\211",8);
 label(c,0,152,en?"B/SELECT: BACK  PAUSED":"B/SELECT: \215\200\207\200\204  \217\200\223\207\200",8);
}
