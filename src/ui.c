#include "dok/ui.h"
#include "ui_native_text.inc"
static size_t length(const char *s){size_t n=0;while(s[n])++n;return n;}
void dok_ui_init(dok_ui *u){*u=(dok_ui){0};u->state=DOK_UI_TITLE;u->object=UINT16_MAX;}
unsigned dok_ui_input(dok_ui *u,uint16_t keys){
 unsigned pressed=keys&~u->last_keys,changed=0;u->last_keys=keys;
 if(u->state==DOK_UI_PLAY){
  if((keys&12)==12 && (pressed&12)){u->state=DOK_UI_PAUSE;u->focus=0;u->blocked|=keys;}
 }else if(u->state==DOK_UI_HELP || u->state==DOK_UI_STATUS){
  if(pressed&(2|4|8)){u->state=u->return_state;u->blocked|=keys;}
 }else{
  unsigned rows=u->state==DOK_UI_TITLE?6:5;
  if(u->state==DOK_UI_TITLE && u->focus==4){if(pressed&16)u->start_level=(u->start_level+1)%3;if(pressed&32)u->start_level=(u->start_level+2)%3;}
  if(pressed&64)u->focus=(u->focus+rows-1)%rows;
  if(pressed&128)u->focus=(u->focus+1)%rows;
  if(pressed&4){if(u->state==DOK_UI_PAUSE){u->state=0;u->blocked|=keys;}else {u->language^=1;changed=1;}}
  else if((pressed&2) && u->state==DOK_UI_PAUSE){u->state=0;u->blocked|=keys;}
  else if(pressed&(1|8)){
   if(u->focus==0){u->action=1;u->state=0;u->blocked|=keys;}
   else if(u->focus==1){u->language^=1;changed=1;}
   else if(u->focus==2 || u->focus==3){u->return_state=u->state;u->state=u->focus==2?DOK_UI_HELP:DOK_UI_STATUS;}
   else if(u->state==DOK_UI_TITLE){u->action=u->focus==4?2:3;u->blocked|=keys;}else {u->state=DOK_UI_TITLE;u->focus=0;}
  }
 }
 return changed;
}
uint16_t dok_ui_game_keys(dok_ui *u,uint16_t keys){
 u->blocked&=keys;
 return u->state?0:(uint16_t)(keys&~u->blocked&~4u);
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
   *text=l->text+start;return n>DOK_UI_TEXT_LIMIT?DOK_UI_TEXT_LIMIT:n;
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
#include "ui_text.inc"
static const char *const ui_end[2]={"\202\233\225\216\204", "END / CREDITS"};
static const char *const ui_new[2]={"\215\216\202\200\237 \210\203\220\200", "NEW GAME"};
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
 unsigned hpw=20+digits(w->player.health)*8,epw=16+digits(w->player.energy)*8,ex=240-epw;
 panel(&c,0,0,hpw,14);panel(&c,ex,0,epw,14);
 if(art){icon(&c,&art->health,3,3);icon(&c,&art->energy,ex+3,3);}
 number(&c,17,3,w->player.health);number(&c,ex+13,3,w->player.energy);
 if(art){unsigned x=240;for(unsigned i=0;i<3;i++)if(w->player.keys&(1u<<i)){
  x-=16;panel(&c,x,128,16,16);icon(&c,&art->keys[i],x+3,130);
 }}
 panel(&c,0,144,240,16);uint8_t message[60];size_t n=dok_ui_text(u,w->level,message);
 message_line(&c,message,n);
 if(!u->state)return;
 panel(&c,8,14,224,116);
 if(u->state==DOK_UI_HELP){
  label(&c,16,20,ui_help[en][0],16);for(unsigned i=1;i<8;i++)label(&c,16,36+i*10,ui_help[en][i],8);
 }else if(u->state==DOK_UI_STATUS){
  label(&c,16,20,ui_status[en][0],16);
  for(unsigned i=1;i<5;i++)label(&c,16,38+i*14,ui_status[en][i],8);
  number(&c,176,52,w->player.health_max);number(&c,176,66,w->player.energy_max);
  number(&c,176,80,w->player.score);number(&c,176,94,w->player.map_revealed);
  label(&c,16,106,ui_keyhint[en],8);
  label(&c,16,116,ui_back[en],8);
 }else{
  label(&c,16,20,u->state==DOK_UI_TITLE?ui_title[en]:ui_pause[en],16);
  unsigned rows=u->state==DOK_UI_TITLE?6:5,spacing=u->state==DOK_UI_TITLE?12:14;
  for(unsigned i=0;i<rows;i++){
   if(i==u->focus){box(&c,14,44+i*spacing,212,u->state==DOK_UI_TITLE?10:12,fg);c.fg=bg;}
   label(&c,18,46+i*spacing,(i==5)?ui_end[en]:(i==4 && u->state==DOK_UI_TITLE)?ui_new[en]:ui_menu[en][i],8);
   if(i==4 && u->state==DOK_UI_TITLE)number(&c,194,46+i*spacing,(int)u->start_level+1);
   if(i==1)label(&c,184,46+i*spacing,en?"ENG":"RUS",8);
   c.fg=fg;
  }
  label(&c,16,116,ui_hint[en],8);
 }
}

/* Full-screen original CP866 text: CR ignored, LF line break, $/NUL end.
 * Thirty columns by fourteen lines, pages preserve all source bytes. */
unsigned dok_ui_screen_pages(const uint8_t *s,size_t n){unsigned row=0,col=0;for(size_t i=0;i<n && s[i] && s[i]!='$';i++){if(s[i]=='\r')continue;if(s[i]=='\n'){++row;col=0;}else{if(col==30){++row;col=0;}++col;}}return row/14+1;}
void dok_ui_screen(uint8_t *f,const uint8_t *f8,const uint8_t *f16,unsigned fg,unsigned bg,const uint8_t *s,size_t n,unsigned page,const char *heading,unsigned language){
 canvas c={f,f8,f16,fg,bg};panel(&c,0,0,240,160);label(&c,0,0,heading,16);
 unsigned row=0,col=0;for(size_t i=0;i<n && s[i] && s[i]!='$';i++){unsigned ch=s[i];if(ch=='\r')continue;if(ch=='\n'){++row;col=0;continue;}if(col==30){++row;col=0;}if(row/14==page){uint8_t b=(uint8_t)(ch<32?' ':ch);text(&c,col*8,20+(row%14)*8,&b,1,8);}++col;}
 uint8_t footer[60];const char *hint=language?"A/START: OK B: BACK":"A/START: OK B: \215\200\207\200\204";
 size_t count=length(hint);for(size_t i=0;i<count;i++)footer[i]=(uint8_t)hint[i];
 footer[count++]=' ';
 unsigned values[2]={page+1,dok_ui_screen_pages(s,n)};
 for(unsigned i=0;i<2;i++){
  uint8_t digits[10];unsigned used=0,v=values[i];do{digits[used++]=(uint8_t)('0'+v%10);v/=10;}while(v);
  while(used)footer[count++]=digits[--used];if(!i)footer[count++]='/';
 }
 message_line(&c,footer,count);
}
