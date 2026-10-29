#include "dok/ui.h"
#include "ui_native_text.inc"
static size_t length(const char *s){size_t n=0;while(s[n])++n;return n;}
void dok_ui_init(dok_ui *u){*u=(dok_ui){0};u->state=DOK_UI_SPLASH;u->object=UINT16_MAX;}
unsigned dok_ui_frontend(const dok_ui *u){return u->state!=DOK_UI_PLAY;}
unsigned dok_ui_input(dok_ui *u,uint16_t keys){
 unsigned pressed=keys&~u->last_keys,changed=0;u->last_keys=keys;
 if(u->state==DOK_UI_PLAY){
  if((keys&12)==12 && (pressed&12)){u->state=DOK_UI_PAUSE;u->focus=0;u->blocked|=keys;}
  else if(pressed&4){u->state=DOK_UI_MAP;u->map_centered=0;u->map_repeat=0;u->blocked|=keys;}
 }else if(u->state==DOK_UI_MAP){
  if((keys&12)==12 && (pressed&12)){u->state=DOK_UI_PAUSE;u->focus=0;u->blocked|=keys;}
  else if(pressed&(2|4)){u->state=DOK_UI_PLAY;u->blocked|=keys;}
  else if(pressed&1)u->map_centered=0;
  else if(keys&240){
   if((pressed&240) || ++u->map_repeat>=12){
    if(u->map_repeat>=12)u->map_repeat=9;
    u->map_x+=(keys&16?6:0)-(keys&32?6:0);
    u->map_y+=(keys&128?6:0)-(keys&64?6:0);
   }
  }else u->map_repeat=0;
 }else if(u->state==DOK_UI_SPLASH){
  if(pressed&(1|8)){u->state=DOK_UI_TITLE;u->focus=0;u->blocked|=keys;}
 }else if(u->state==DOK_UI_LEVELS){
  if(pressed&64)u->focus=(u->focus+2)%3;
  if(pressed&128)u->focus=(u->focus+1)%3;
  if(pressed&4){u->language^=1;changed=1;}
  else if(pressed&2){u->state=DOK_UI_TITLE;u->focus=0;u->blocked|=keys;}
  else if(pressed&(1|8)){u->start_level=u->focus;u->action=DOK_UI_NEW_GAME;u->blocked|=keys;}
 }else if(u->state==DOK_UI_HELP || u->state==DOK_UI_STATUS){
  if(pressed&(2|4|8)){u->state=u->return_state;u->blocked|=keys;}
 }else{
  unsigned rows=u->state==DOK_UI_TITLE?5:4;
  if(pressed&64)u->focus=(u->focus+rows-1)%rows;
  if(pressed&128)u->focus=(u->focus+1)%rows;
  if(pressed&4){if(u->state==DOK_UI_PAUSE){u->state=0;u->blocked|=keys;}else {u->language^=1;changed=1;}}
  else if((pressed&2) && u->state==DOK_UI_PAUSE){u->state=0;u->blocked|=keys;}
  else if(pressed&(1|8)){
   if(u->focus==0){if(u->state==DOK_UI_TITLE){u->state=DOK_UI_LEVELS;u->focus=u->start_level;}else {u->action=DOK_UI_RESUME;u->state=0;}u->blocked|=keys;}
   else if(u->focus==1){u->language^=1;changed=1;}
   else if(u->focus==2){u->return_state=u->state;u->state=DOK_UI_HELP;}
   else if(u->state==DOK_UI_TITLE){u->action=u->focus==3?DOK_UI_STORY:DOK_UI_CREDITS;u->blocked|=keys;}else {u->state=DOK_UI_TITLE;u->focus=0;u->blocked|=keys;}
  }
 }
 return changed;
}
uint16_t dok_ui_game_keys(dok_ui *u,uint16_t keys){
 u->blocked&=keys;
 return u->state?0:(uint16_t)(keys&~u->blocked&~4u);
}
/* Approved GBA presentation copy; original LEVEL text pools stay intact. */
static size_t notification_copy(unsigned language,const uint8_t **text,size_t n){
 static const char *const source[2]={"\040\212\216\202\230 \206\210\202\216\211 \202\216\204\233 \204\200\213 10 \213\205\222 \206\210\207\215\210","\040\203\216\220\230\216\212 \204\216\201\220\216\211 \212\200\230\210 \204\200\213 15 \213\205\222 \206\210\207\215\210"};
 static const char *const display[2]={"\206\210\202\200\237 \202\216\204\200 \204\200\213\200 10 \213\205\222 \206\210\207\215\210","\204\216\201\220\200\237 \212\200\230\200 \204\200\213\200 15 \213\205\222 \206\210\207\215\210"};
 if(language)return n;
 for(unsigned i=0;i<2;++i){
  if(n!=length(source[i]))continue;
  size_t j=0;while(j<n && (*text)[j]==(uint8_t)source[i][j])++j;
  if(j==n){*text=(const uint8_t *)display[i];return length(display[i]);}
 }
 return n;
}
static size_t resolve(dok_ui *u,const dok_runtime_level *l,const uint8_t **text){
 if(u->object==UINT16_MAX-1){if(u->slot>=7)return 0;*text=(const uint8_t *)ui_native[u->language][u->slot];return length((const char *)*text);}
 if(u->object==UINT16_MAX){*text=u->literal;return u->literal_size;}
 if(!l || u->object>=l->object_count)return 0;
 const dok_runtime_definition *d=&l->definitions[u->object];
 for(unsigned i=0;i<d->field_count;i++){
  const dok_runtime_field *f=&l->fields[d->field_begin+i];
  if(f->native_offset==u->slot && f->kind==DOK_LEVEL_TEXT){
   unsigned start=f->value.bytes.offset,n=f->value.bytes.size;
   if(start>l->text_size || n>l->text_size-start){++u->errors;return 0;}
   *text=l->text+start;n=(unsigned)notification_copy(u->language,text,n);
   return n>DOK_UI_TEXT_LIMIT?DOK_UI_TEXT_LIMIT:n;
  }
 }return 0;
}
static void next(dok_ui *u,uint32_t now){
 if(!u->count){u->active=0;return;}
 dok_ui_message m=u->queue[u->read];u->read=(u->read+1)%4;--u->count;
 u->object=m.object;u->slot=m.slot;u->active=1;u->page=0;u->deadline=now+30000;
}
void dok_ui_notify(dok_ui *u,uint16_t object,uint16_t slot,uint32_t now){
 if(u->count==4){u->read=(u->read+1)%4;--u->count;}
 unsigned at=(u->read+u->count)%4;u->queue[at]=(dok_ui_message){object,slot};++u->count;++u->messages;
 if(!u->active)next(u,now);
}
void dok_ui_literal(dok_ui *u,const uint8_t *s,size_t n,uint32_t now){
 if(n>DOK_UI_TEXT_LIMIT)n=DOK_UI_TEXT_LIMIT;
 for(size_t i=0;i<n;i++)u->literal[i]=s[i];u->literal_size=(uint16_t)n;u->object=UINT16_MAX;u->slot=0;
 u->active=1;u->page=0;u->deadline=now+30000;++u->messages;
}
void dok_ui_tick(dok_ui *u,const dok_runtime_level *l,uint32_t now){
 for(unsigned i=0;i<8 && u->active;i++){
  const uint8_t *s=0;u->length=(uint32_t)resolve(u,l,&s);u->pages=(u->length+59)/60;
  if(!u->length){next(u,now);continue;}
  if(u->page>=u->pages)u->page=u->pages-1;
  if((int32_t)(now-u->deadline)<0)break;
  if(u->page+1<u->pages){++u->page;u->deadline=now+30000;}else next(u,now);
 }
}
size_t dok_ui_text(dok_ui *u,const dok_runtime_level *l,uint8_t out[60]){
 const uint8_t *s=0;size_t n=resolve(u,l,&s),start=u->page*60;
 if(!u->active || start>=n)return 0;
 n-=start;if(n>60)n=60;
 for(size_t i=0;i<n;i++){unsigned ch=s[start+i];out[i]=(uint8_t)(ch<32?' ':ch);}
 return n;
}
typedef struct {uint8_t *f;const uint8_t *f8,*f16;unsigned fg,bg;} canvas;
#ifdef DOK_RENDER_HW_DIRECT_PROTOTYPE
#include "ui_tiled.inc"
#endif
static const dok_ui_art *art;
void dok_ui_set_art(const dok_ui_art *value){art=value;}
static void screen_copy(uint8_t *dst,const uint8_t *src){
 /* Packed, aligned ROM art; use word transfers on GBA without a decoded
  * image buffer. Host callers may provide an unaligned guard buffer. */
 if(!(((uintptr_t)dst|(uintptr_t)src)&3u)){
  typedef uint32_t ui_word __attribute__((may_alias));
  ui_word *d=(ui_word *)(void *)dst;const ui_word *s=(const ui_word *)(const void *)src;
  for(unsigned i=0;i<9600;++i)d[i]=s[i];
 }else for(unsigned i=0;i<38400;++i)dst[i]=src[i];
}
const uint8_t *dok_ui_palette(const dok_ui *u){return art && dok_ui_frontend(u) && u->state!=DOK_UI_MAP?(u->state==DOK_UI_SPLASH?art->splash_palette:art->menu_palette):0;}
static void box(canvas *c,unsigned x,unsigned y,unsigned w,unsigned h,unsigned color){
 if(x>=240 || y>=160)return;
 if(w>240-x)w=240-x;if(h>160-y)h=160-y;
#ifdef DOK_RENDER_HW_DIRECT_PROTOTYPE
 if(g_hw_tile_active){for(unsigned r=0;r<h;++r)for(unsigned col=0;col<w;++col)UI_PUT(c,x+col,y+r,color);return;}
#endif
#if defined(__GNUC__)
 typedef uint32_t fill_word __attribute__((may_alias));
#else
 typedef uint32_t fill_word;
#endif
 uint32_t fill=(uint8_t)color*UINT32_C(0x01010101);
 for(unsigned r=y;r<y+h;r++){
  uint8_t *dst=c->f+r*240+x;unsigned n=w;
  while(n && ((uintptr_t)dst&3)){*dst++=(uint8_t)color;--n;}
  while(n>=4){*(fill_word *)(void *)dst=fill;dst+=4;n-=4;}
  while(n--)*dst++=(uint8_t)color;
 }
}
static void text(canvas *c,unsigned x,unsigned y,const uint8_t *s,size_t n,unsigned height){
 const uint8_t *font=height==16?c->f16:c->f8;
 for(size_t i=0;i<n && x+8<=240;i++,x+=8)for(unsigned r=0;r<height && y+r<160;r++){
  unsigned bits=font[(unsigned)s[i]*height+r];for(unsigned col=0;col<8;col++)if(bits&(128u>>col))
#ifdef DOK_RENDER_HW_DIRECT_PROTOTYPE
   UI_PUT(c,x+col,y+r,c->fg);
#else
   c->f[(y+r)*240+x+col]=(uint8_t)c->fg;
#endif
 }
}
/* One centered line inside the three-pixel panel bevel. Condense FONT8
 * horizontally only when needed; OR source columns so thin strokes survive. */
static void message_line(canvas *c,const uint8_t *s,size_t n){
 while(n && *s==' '){++s;--n;}while(n && s[n-1]==' ')--n;
 if(!n)return;
 unsigned width=n*8<=234?8:234/(unsigned)n;
 if(!width)width=1;
 unsigned x=(240-(unsigned)n*width)/2;
 uint8_t masks[8]={0};
 for(unsigned col=0;col<width;col++)for(unsigned k=col*8/width;k<(col+1)*8/width;k++)masks[col]|=(uint8_t)(128u>>k);
 for(size_t i=0;i<n;i++)for(unsigned r=0;r<8;r++){
  unsigned bits=c->f8[(unsigned)s[i]*8+r];
  for(unsigned col=0;col<width;col++){
   if(bits&masks[col])
#ifdef DOK_RENDER_HW_DIRECT_PROTOTYPE
    UI_PUT(c,x+(unsigned)i*width+col,148+r,c->fg);
#else
    c->f[(148+r)*240+x+(unsigned)i*width+col]=(uint8_t)c->fg;
#endif
  }
 }
}
/* Keep the original panel's three-pixel bevel; repeat its plain interior.
 * No additional palette, framebuffer or decoded asset copy is needed. */
static void panel(canvas *c,unsigned x,unsigned y,unsigned w,unsigned h){
 if(!art || !art->panel.pixels){box(c,x,y,w,h,c->bg);return;}
 const dok_image_view *p=&art->panel;
 box(c,x,y,w,h,p->pixels[5*p->width+10]);
 for(unsigned r=0;r<h && y+r<160;r++){
  unsigned sy=r<3?r:r+3>=h?p->height-(h-r):5;
  unsigned count=r<3 || r+3>=h?w:6;
  for(unsigned i=0;i<count;i++){
   unsigned col=count==6 && i>=3?w-6+i:i;
   if(x+col>=240)continue;
   unsigned sx=col<3?col:col+3>=w?p->width-(w-col):10;
#ifdef DOK_RENDER_HW_DIRECT_PROTOTYPE
   UI_PUT(c,x+col,y+r,p->pixels[sy*p->width+sx]);
#else
   c->f[(y+r)*240+x+col]=p->pixels[sy*p->width+sx];
#endif
  }
 }
}
static void icon(canvas *c,const dok_image_view *p,unsigned x,unsigned y){
 if(!p || !p->pixels)return;
 for(unsigned r=0;r<p->height && y+r<160;r++)for(unsigned col=0;col<p->width && x+col<240;col++){
  unsigned pixel=p->pixels[r*p->width+col];
  if(pixel)
#ifdef DOK_RENDER_HW_DIRECT_PROTOTYPE
   UI_PUT(c,x+col,y+r,pixel);
#else
   c->f[(y+r)*240+x+col]=(uint8_t)pixel;
#endif
 }
}
static unsigned digits(int n){unsigned count=n<0?1:0,v=n<0?0u-(unsigned)n:(unsigned)n;do{++count;v/=10;}while(v);return count;}
static void label(canvas *c,unsigned x,unsigned y,const char *s,unsigned h){text(c,x,y,(const uint8_t *)s,length(s),h);}
static void number(canvas *c,unsigned x,unsigned y,int n){
 uint8_t b[7];unsigned count=0,v=n<0?0u-(unsigned)n:(unsigned)n;
 do {b[count++]=(uint8_t)('0'+v%10);v/=10;}while(v && count<6);
 if(n<0){label(c,x,y,"-",8);x+=8;}
 while(count)text(c,x,y,&b[--count],1,8),x+=8;
}
static const dok_runtime_actor *map_player(const dok_runtime *w){
 if(!w->level || !w->player_index || w->player_index>w->object_count)return 0;
 unsigned index=w->level->definitions[w->player_index-1].actor_index;
 return index<w->actor_count?&w->actors[index]:0;
}
void dok_ui_explore(dok_runtime *w){
 if(!w || !w->level || !w->player_index)return;
 const dok_runtime_actor *a=map_player(w);
 if(!a)return;
 const dok_runtime_level *l=w->level;int width=l->settings[DOK_LEVEL_WIDTH],height=l->settings[DOK_LEVEL_LENGTH],side=l->settings[DOK_LEVEL_CELL_SIDE];
 if(!side)return;int px=a->x/side,pz=a->z/side;
 if(px<0 || px>=width || pz<0 || pz>=height)return;
 static const dok_runtime_level *last_level;static int last_cell=-1;
 int current=pz*width+px;
 if(last_level==l && last_cell==current && w->maps[3*l->cells+current])return;
 last_level=l;last_cell=current;
 /* GBA discovery uses a bounded sight fan, evaluated on cell entry. Adjacent
  * rays cover the room without holes; walls/closed doors stop each ray.
  * Reuse the existing per-level plane; map pickups reveal the entire plan. */
 for(int tz=-10;tz<=10;++tz)for(int tx=-10;tx<=10;++tx){
  if(tx!=-10 && tx!=10 && tz!=-10 && tz!=10)continue;
  int x=px,z=pz,dx=tx<0?-tx:tx,dz=tz<0?tz:-tz,sx=tx<0?-1:1,sz=tz<0?-1:1,error=dx+dz;
  for(unsigned step=0;step<=20;++step){
   if(x<0 || x>=width || z<0 || z>=height)break;
   unsigned cell=(unsigned)(z*width+x);w->maps[3*l->cells+cell]=1;
   unsigned token=l->glyph_index[w->maps[l->cells+cell]];
   if(!token)break;
   unsigned id=token-1,cls=l->definitions[id].class_id;
   if((step && (cls==2 || (cls==7 && !(w->objects[id].flags&16)))) || (x==px+tx && z==pz+tz))break;
   int twice=error*2;if(twice>=dz){error+=dz;x+=sx;}if(twice<=dx){error+=dx;z+=sz;}
  }
 }
}
#include "ui_map.inc"
#include "ui_text.inc"
static const char *const ui_end[2]={"\202\233\225\216\204", "END / CREDITS"};
#ifdef DOK_RENDER_HW_UI_REUSE_PROTOTYPE
unsigned g_hw_ui_changed;
static unsigned hw_ui_valid,hw_ui_key[11];
static uint8_t hw_ui_message[60];
static int hw_ui_cached(dok_ui *u,uint8_t *frame,const dok_runtime *w,const uint8_t *f8,const uint8_t *f16,unsigned fg,unsigned bg) {
 if(!g_hw_tile_active){hw_ui_valid=0;return 0;}
 uint8_t message[60];unsigned n=(unsigned)dok_ui_text(u,w->level,message);
 unsigned key[11]={(unsigned)(uintptr_t)frame,(unsigned)(uintptr_t)w->level,(unsigned)(uintptr_t)art,
  (unsigned)(uintptr_t)f8,(unsigned)(uintptr_t)f16,fg,bg,u->language,(unsigned)(uint16_t)w->player.health|
  ((unsigned)(uint16_t)w->player.energy<<16),w->player.keys,n};
 unsigned same=hw_ui_valid;
 for(unsigned i=0;i<11;++i)if(key[i]!=hw_ui_key[i])same=0;
 for(unsigned i=0;i<n;++i)if(message[i]!=hw_ui_message[i])same=0;
 g_hw_ui_changed=!same;if(same)return 1;
 for(unsigned i=0;i<11;++i)hw_ui_key[i]=key[i];
 for(unsigned i=0;i<n;++i)hw_ui_message[i]=message[i];
 extern void dok_gba_tiled_clear(uint8_t *,unsigned);
 dok_gba_tiled_clear(frame+18432,6784);hw_ui_valid=1;return 0;
}
#endif
void dok_ui_draw(dok_ui *u,uint8_t *frame,const dok_runtime *w,const uint8_t *f8,const uint8_t *f16,unsigned fg,unsigned bg){
 if(!u || !frame || !w || !f8 || !f16)return;
#ifdef DOK_RENDER_HW_UI_REUSE_PROTOTYPE
 if(hw_ui_cached(u,frame,w,f8,f16,fg,bg))return;
#endif
 canvas c={frame,f8,f16,fg,bg};unsigned en=u->language;
 unsigned frontend=dok_ui_frontend(u);
 if(u->state==DOK_UI_SPLASH && art && art->splash.pixels){
  if(art->splash_frame){screen_copy(frame,art->splash_frame);return;}
  const dok_image_view *p=&art->splash;
  /* Validated320x200 source. Integer phase stepping avoids software divide
   * for every pixel on ARM7. Keep the original copyright strip at1:1. */
  for(unsigned y=0;y<160;++y){
   const uint8_t *row=p->pixels+(y+(y>>2))*320;
   for(unsigned x=0,sx=0,phase=0;x<240;++x,++sx){frame[y*240+x]=row[sx];if(++phase==3){phase=0;++sx;}}
  }
  for(unsigned y=0;y<11;++y)for(unsigned x=0;x<81;++x){
   unsigned pixel=p->pixels[(189+y)*320+x];
   frame[(149+y)*240+x]=(uint8_t)(x>=58 && pixel!=196?0:pixel);
  }
  return;
 }
 if(frontend){
  if(art && art->menu_frame)screen_copy(frame,art->menu_frame);
  else if(art && art->background.pixels){const dok_image_view *p=&art->background;
   unsigned sy=0;
   for(unsigned y=0;y<160;++y){const uint8_t *row=p->pixels+sy*p->width;unsigned sx=0;
    for(unsigned x=0;x<240;++x){frame[y*240+x]=row[sx];if(++sx==p->width)sx=0;}
    if(++sy==p->height)sy=0;
   }
  }else box(&c,0,0,240,160,bg);
 }else {
 unsigned hpw=20+digits(w->player.health)*8,epw=16+digits(w->player.energy)*8,ex=240-epw;
 panel(&c,0,0,hpw,14);panel(&c,ex,0,epw,14);
 if(art){icon(&c,&art->health,3,3);icon(&c,&art->energy,ex+3,3);}
 number(&c,17,3,w->player.health);number(&c,ex+13,3,w->player.energy);
 if(art){unsigned x=240;for(unsigned i=0;i<3;i++)if(w->player.keys&(1u<<i)){
  x-=16;panel(&c,x,128,16,16);icon(&c,&art->keys[i],x+3,130);
 }}
 panel(&c,0,144,240,16);uint8_t message[60];size_t n=dok_ui_text(u,w->level,message);
 message_line(&c,message,n);
 }
 if(frontend && (u->state==DOK_UI_PAUSE ||
    ((u->state==DOK_UI_HELP || u->state==DOK_UI_STATUS) && u->return_state==DOK_UI_PAUSE))){
  panel(&c,0,144,240,16);uint8_t message[60];size_t n=dok_ui_text(u,w->level,message);message_line(&c,message,n);
 }
 if(!u->state)return;
 if(u->state==DOK_UI_MAP){map_draw(&c,w,u,en);return;}
 panel(&c,8,14,224,116);
 if(u->state==DOK_UI_HELP){
  label(&c,16,20,ui_help[en][0],16);for(unsigned i=1;i<9;i++)label(&c,16,34+i*10,ui_help[en][i],8);
 }else if(u->state==DOK_UI_STATUS){
  label(&c,16,20,ui_status[en][0],16);
  for(unsigned i=1;i<5;i++)label(&c,16,38+i*14,ui_status[en][i],8);
  number(&c,176,52,w->player.health_max);number(&c,176,66,w->player.energy_max);
  number(&c,176,80,w->player.score);number(&c,176,94,w->player.map_revealed);
  label(&c,16,106,ui_keyhint[en],8);
  label(&c,16,116,ui_back[en],8);
 }else if(u->state==DOK_UI_LEVELS){
  label(&c,16,20,ui_choose[en],16);
  for(unsigned i=0;i<3;i++){
   if(i==u->focus){box(&c,14,48+i*22,212,16,fg);c.fg=bg;}
   label(&c,22,52+i*22,ui_levels[en][i],8);c.fg=fg;
  }
  label(&c,16,116,ui_level_hint[en],8);
 }else{
  label(&c,16,20,u->state==DOK_UI_TITLE?ui_title[en]:ui_pause[en],16);
  unsigned rows=u->state==DOK_UI_TITLE?5:4,spacing=u->state==DOK_UI_TITLE?14:17;
  for(unsigned i=0;i<rows;i++){
   if(i==u->focus){box(&c,14,44+i*spacing,212,12,fg);c.fg=bg;}
   label(&c,18,46+i*spacing,(i==0 && u->state==DOK_UI_TITLE)?ui_start[en]:(i==3 && u->state==DOK_UI_TITLE)?ui_story[en]:(i==4 && u->state==DOK_UI_TITLE)?ui_end[en]:ui_menu[en][i],8);
   if(i==1)label(&c,184,46+i*spacing,en?"ENG":"RUS",8);
   c.fg=fg;
  }
  label(&c,16,116,u->state==DOK_UI_TITLE?ui_hint[en]:ui_back[en],8);
 }
}

/* Keep FONT8 legible on the original LCD. Reflow source lines into paragraphs,
 * wrapping whole words inside the 8px margins; never shrink the glyphs.
 * One shared layout owns drawing and pagination, including the last page. */
static void screen_row(canvas *c,const uint8_t *line,unsigned used,unsigned row,unsigned page){
 if(c && row/14==page)text(c,8,24+(row%14)*8,line,used,8);
}
static unsigned screen_paragraph(canvas *c,const uint8_t *s,size_t n,unsigned page,unsigned rows){
 uint8_t line[28];unsigned used=0;size_t i=0;
 while(i<n && s[i] && s[i]!='$'){
  while(i<n && s[i] && s[i]!='$' && s[i]<=32)++i;
  if(i==n || !s[i] || s[i]=='$')break;
  size_t end=i;while(end<n && s[end]>32 && s[end]!='$')++end;
  unsigned word=(unsigned)(end-i);
  if(used && used+1+word>28){
   /* Carry a single-letter word with the next word when it fits. CP866
    * Russian letters occupy80..AF/E0..F1; punctuation stays attached. */
   unsigned ch=line[used-1],carry=word<=26 && (used==1 || line[used-2]==' ') &&
    ((ch>='A' && ch<='Z') || (ch>='a' && ch<='z') || (ch>=128 && ch<=175) || (ch>=224 && ch<=241));
   if(carry)used=used>1?used-2:0;
   if(used)screen_row(c,line,used,rows++,page);
   used=0;if(carry)line[used++]=(uint8_t)ch;
  }
  if(used)line[used++]=' ';
  /* Defensive fallback for a token wider than the screen. Original text
   * words fit; malformed/user-supplied strings still progress in bounds. */
  while(i<end){
   if(used==28){screen_row(c,line,used,rows++,page);used=0;}
   line[used++]=s[i++];
  }
 }
 if(used)screen_row(c,line,used,rows++,page);
 return rows;
}
static unsigned screen_layout(canvas *c,const uint8_t *s,size_t n,unsigned page){
 size_t i=0;unsigned rows=0;
 while(i<n && s[i] && s[i]!='$'){
  while(i<n && s[i] && s[i]!='$' && s[i]<=32)++i;
  if(i==n || !s[i] || s[i]=='$')break;
  size_t start=i,end=i;
  while(i<n && s[i] && s[i]!='$'){
   if(s[i]>32){end=++i;continue;}
   unsigned breaks=0;
   while(i<n && s[i] && s[i]!='$' && s[i]<=32){if(s[i]=='\n')++breaks;++i;}
   if(breaks>1)break;
  }
  unsigned count=screen_paragraph(0,s+start,end-start,0,0);
  if(rows && rows%14)++rows; /* one blank line between paragraphs */
  /* A short paragraph starts intact on the next page. In particular, do
   * not strand the final sentence on an otherwise empty second page. */
  if(count<=14 && rows%14+count>14)rows+=14-rows%14;
  rows=screen_paragraph(c,s+start,end-start,page,rows);
 }
 return rows?(rows+13)/14:1;
}
unsigned dok_ui_screen_pages(const uint8_t *s,size_t n){return screen_layout(0,s,n,0);}
void dok_ui_screen(uint8_t *f,const uint8_t *f8,const uint8_t *f16,unsigned fg,unsigned bg,const uint8_t *s,size_t n,unsigned page,const char *heading,unsigned language){
 canvas c={f,f8,f16,fg,bg};panel(&c,0,0,240,160);label(&c,8,4,heading,16);
 unsigned pages=screen_layout(&c,s,n,page);
 uint8_t footer[60];const char *hint=language?"A/START: OK B: BACK":"A/START: OK B: \215\200\207\200\204";
 size_t count=length(hint);for(size_t i=0;i<count;i++)footer[i]=(uint8_t)hint[i];
 footer[count++]=' ';
 unsigned values[2]={page+1,pages};
 for(unsigned i=0;i<2;i++){
  uint8_t digits[10];unsigned used=0,v=values[i];do{digits[used++]=(uint8_t)('0'+v%10);v/=10;}while(v);
  while(used)footer[count++]=digits[--used];if(!i)footer[count++]='/';
 }
 message_line(&c,footer,count);
}
