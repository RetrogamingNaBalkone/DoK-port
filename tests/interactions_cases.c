#include "dok/interactions.h"
#include "dok/collision.h"
#include "dok/movement.h"
#define CHECK(x) do {if(!(x)) return __LINE__;} while(0)
typedef struct {unsigned locked,opened,closed,finish,stat;} notices;
static void sink(void *p,dok_interaction_kind k,uint16_t id,int32_t a,int32_t b)
{(void)id;(void)a;(void)b;notices *n=p;
 if(k==DOK_INTERACTION_LOCKED) n->locked++;
 if(k==DOK_INTERACTION_OPENED) n->opened++;
 if(k==DOK_INTERACTION_CLOSED) n->closed++;
 if(k==DOK_INTERACTION_FINISH) n->finish++;
 if(k==DOK_INTERACTION_STAT) n->stat++;
}
unsigned dok_interactions_cases(dok_runtime *w,dok_interactions *s)
{
 dok_runtime_definition defs[5]={{0}};dok_runtime_field fields[4]={{0}};
 dok_runtime_level l={0};notices n={0};
 l.object_count=5;l.definitions=defs;l.fields=fields;l.field_count=4;l.cells=25;
 l.settings[DOK_LEVEL_WIDTH]=5;l.settings[DOK_LEVEL_LENGTH]=5;
 l.settings[DOK_LEVEL_CELL_SIDE]=480;l.settings[DOK_LEVEL_MIN_DISTANCE]=238;
 unsigned glyphs[5]={' ','P','d','f','e'},classes[5]={0,1,7,13,12};
 for(unsigned i=0;i<5;i++) {defs[i].glyph=(uint8_t)glyphs[i];defs[i].class_id=(uint16_t)classes[i];l.glyph_index[glyphs[i]]=(uint16_t)(i+1);w->objects[i]=(dok_runtime_object){0};}
 w->level=&l;w->object_count=5;w->actor_count=1;w->player_index=2;w->pickup_99_total=0;
 for(unsigned i=0;i<100;i++) w->maps[i]=' ';
 w->objects[0].flags=0x10;w->objects[1].flags=0x10;
 w->objects[2].flags=64;
 dok_runtime_door *d=&w->objects[2].state.door;
 d->parameter_54=2;d->parameter_55=1;d->original_flags=64;d->extent=1;d->parameter_5c=3;
 dok_runtime_actor a={0};a.x=1200;a.z=1200;a.glyph='P';a.heading=0;
 w->maps[12]='P';w->maps[25+17]='d';w->maps[25+18]='d';
 CHECK(!dok_collision_clear(w,1200,1680,238));
 dok_interactions_init(s,w,0,sink,&n);w->player.keys=0;
 dok_interactions_use(s,&a);CHECK(n.locked==1 && !s->owners[2].count);
 w->player.keys=2;dok_interactions_use(s,&a);
 CHECK(s->owners[2].count==1 && d->extent==30 && d->parameter_55==1);
 dok_interactions_poll(s,415);CHECK(w->objects[2].frame==0);
 dok_interactions_poll(s,416);CHECK(w->objects[2].frame==1 && !d->parameter_55);
 CHECK(dok_interactions_event(s,2,0x800,1)==0x800 && s->owners[2].count==1);
 for(unsigned i=0;i<6;i++) dok_interactions_poll(s,750+i*334);
 CHECK(w->objects[2].frame==7 && !(w->objects[2].flags&0x10));
 dok_interactions_poll(s,3000);CHECK(w->objects[2].frame==8 && (w->objects[2].flags&0x10));
 CHECK(dok_collision_clear(w,1200,1680,238));
 for(unsigned i=0;i<22;i++) dok_interactions_poll(s,3334+i*334);
 CHECK(d->parameter_54==1 && d->parameter_55 && n.opened==1 && !s->owners[2].count);
 /* Native mode0 manual close; passage stays clear until final callback. */
 dok_interactions_event(s,2,0x800,1);CHECK(s->owners[2].count==1);
 d->state_56=1;dok_interactions_poll(s,12000);CHECK(w->objects[2].frame==29);
 d->state_56=0;for(unsigned i=0;i<30;i++) dok_interactions_poll(s,12500+i*500);
 CHECK(d->parameter_54==2 && w->objects[2].flags==64 && n.closed==1);
 /* Trigger bypass, entry identity shared across disjoint glyph cells. */
 defs[3].field_begin=0;defs[3].field_count=2;
 fields[0].native_offset=0x66;fields[0].kind=DOK_LEVEL_CHAR;fields[0].value.number='d';
 fields[1].native_offset=0x67;fields[1].value.number=0x800;
 w->maps[50+12]='f';w->player.keys=0;dok_interactions_zone(s,&a);
 CHECK(s->owners[2].count==1);dok_interactions_zone(s,&a);CHECK(s->owners[2].count==1);
 w->maps[50+12]=' ';dok_interactions_zone(s,&a);w->maps[50+12]='f';dok_interactions_zone(s,&a);
 CHECK(s->owners[2].count==2); /* ready is not cleared until first callback */
 /* Class6 base method does not consume Use or create a toggle. */
 defs[0].class_id=6;CHECK(dok_interactions_event(s,0,0x800,1)==0x800);CHECK(!s->owners[0].count);defs[0].class_id=0;
 /* Mode1 autoclose: timer expires, waits for opening and occupancy, then
  * queues CLOSE with the same delay a second time, as original descriptor. */
 dok_interactions_init(s,w,0,sink,&n);w->objects[2].frame=0;d->parameter_54=2;d->parameter_55=1;
 d->parameter_5e=1;d->parameter_66=100;w->objects[2].flags=64;
 dok_interactions_event(s,2,0x800,3);
 for(unsigned i=0;i<80;i++) dok_interactions_poll(s,500+i*500);
 CHECK(d->parameter_54==2 && !s->owners[2].count);
 /* Finish condition is an explicit G18 event, FCMap remains in place. */
 defs[4].field_begin=2;defs[4].field_count=2;fields[2].native_offset=0x66;fields[2].value.number=1000;
 fields[3].native_offset=0x68;fields[3].value.number=1;w->maps[50+12]='e';w->player.score=0;
 dok_interactions_zone(s,&a);CHECK(n.finish==1 && w->maps[50+12]=='e');
 w->player.score=1;dok_interactions_zone(s,&a);CHECK(n.finish==1);
 /* Conveyor displaces with collisions and balances MMap occupancy. */
 fields[2].value.number=100;fields[3].value.number=-1400;
 a.x=1200;a.z=1200;dok_interactions_zone(s,&a);CHECK(a.x==1154 && a.z==1200 && w->maps[12]=='P');
 CHECK(!s->errors);return 0;
}
