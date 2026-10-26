#include "dok/items.h"
#if defined(__arm__)
#define COLD __attribute__((section(".text.rom"),noinline))
#else
#define COLD
#endif
static int16_t word(int32_t n) {uint32_t v=(uint32_t)n&65535u;return (int16_t)(v<32768?v:(int32_t)v-65536);}
static void emit(dok_items *s,dok_item_kind kind,uint16_t id,int16_t category,int16_t value)
{if(s->sink) s->sink(s->context,kind,id,category,value);}
static int16_t field(const dok_runtime_level *l,unsigned id,unsigned offset)
{
 const dok_runtime_definition *d=&l->definitions[id];
 for(unsigned j=0;j<d->field_count;j++) {
  const dok_runtime_field *f=&l->fields[d->field_begin+j];
  if(f->native_offset==offset && f->kind<=DOK_LEVEL_CHAR) return word(f->value.number);
 }
 return 0;
}
COLD void dok_items_init(dok_items *s,dok_runtime *w,dok_item_sink sink,void *context)
{s->world=w;s->sink=sink;s->context=context;}
COLD int dok_items_stat(dok_items *s,int16_t category,int16_t value)
{
 if(!s || !s->world || !s->world->level) return 0;
 dok_runtime_player *p=&s->world->player;int16_t result;
 if(category==0 || category==1) {
  int16_t *current=category?&p->energy:&p->health,max=category?p->energy_max:p->health_max;
  if(value>0 && *current==max) return 0;
  result=word((int32_t)*current+value);
  if(result<0) result=0;else if(result>max) result=max;
  *current=result;
 } else if(category==2 || category==3) {
  int16_t *max=category==2?&p->health_max:&p->energy_max;
  *max=word((int32_t)*max+value);result=*max;
  if(category==2) p->health=result;else p->energy=result;
 } else if(category==10) {p->keys|=(uint16_t)value;result=(int16_t)p->keys;}
 else if(category==99) {p->score=word((int32_t)p->score+value);result=p->score;}
 else if(category==200) {p->map_revealed=1;emit(s,DOK_ITEM_MAP,DOK_RUNTIME_NONE,category,1);return 1;}
 else if(category>=4 && category<=9) {
  int16_t *parameter=&p->parameters_66_70[category-4];
  *parameter=word((int32_t)*parameter+value);result=*parameter;
 } else if(category>=12 && category<=14) {
  int16_t *parameter=&p->parameters_76_7a[category-12];
  *parameter=word((int32_t)*parameter+value);result=*parameter;
 } else return 0;
 emit(s,DOK_ITEM_STAT,DOK_RUNTIME_NONE,category,result);return 1;
}
COLD int dok_items_pickup(dok_items *s)
{
 if(!s || !s->world || !s->world->level) return 0;
 dok_runtime *w=s->world;const dok_runtime_level *l=w->level;
 if(!w->player_index || w->player_index>w->object_count) return 0;
 unsigned actor=l->definitions[w->player_index-1].actor_index;
 if(actor>=w->actor_count) return 0;
 int32_t side=l->settings[DOK_LEVEL_CELL_SIDE];if(!side) return 0;
 int32_t x=w->actors[actor].x/side,z=w->actors[actor].z/side;
 int32_t width=l->settings[DOK_LEVEL_WIDTH],length=l->settings[DOK_LEVEL_LENGTH];
 if(x<0) x=0;if(x>=width) x=width-1;if(z<0) z=0;if(z>=length) z=length-1;
 unsigned cell=(unsigned)(z*width+x);uint8_t *map=&w->maps[l->cells+cell];
 unsigned token=l->glyph_index[*map];if(!token || token>w->object_count) return 0;
 uint16_t id=(uint16_t)(token-1);unsigned cls=l->definitions[id].class_id;
 if(cls!=3 && cls!=10) return 0;
 int16_t category=field(l,id,0x54),value=field(l,id,0x56);
 int16_t slot=field(l,id,0x81);
 if(cls==10 && (slot<0 || slot>=9)) return 0;
 int accepted=dok_items_stat(s,category,value);
 if(cls==3 && !accepted) return 0;
 emit(s,DOK_ITEM_PICKUP,id,category,value);
 /* Native virtual+18 returns false for stat1000. All used items return1.
  * Consume just this cell: definitions/animation/flags are shared by glyph. */
 if(category!=1000) *map=' ';
 if(cls==10) {
  dok_runtime_player *p=&w->player;p->pending_weapon=slot;
  if(!p->weapons[slot]) {
   p->weapons[slot]=token;
   if(p->selected_weapon<0 || p->selected_weapon>=9 || !p->weapons[p->selected_weapon]) p->selected_weapon=slot;
  }
  emit(s,DOK_ITEM_WEAPON,id,slot,p->selected_weapon);
 }
 return 1;
}
