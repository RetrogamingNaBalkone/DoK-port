#include "dok/collision.h"
static void fill(void *p,unsigned value,unsigned size) {unsigned char *b=p;while(size--) *b++=(unsigned char)value;}
/* Caller supplies scratch runtime; its contents must be reset after this suite. */
unsigned dok_collision_cases(dok_runtime *w)
{
    dok_runtime_definition d[4]={{0}};dok_runtime_level l={0};
    l.cells=25;l.settings[DOK_LEVEL_WIDTH]=l.settings[DOK_LEVEL_LENGTH]=5;
    l.settings[DOK_LEVEL_CELL_SIDE]=480;l.settings[DOK_LEVEL_MIN_DISTANCE]=238;
    l.glyph_index[' ']=1;l.glyph_index['#']=2;l.glyph_index['M']=3;l.glyph_index['D']=4;
    d[0].class_id=0;d[1].class_id=2;d[2].class_id=1;d[3].class_id=7;l.definitions=d;
    w->level=&l;w->object_count=4;
    fill(w->maps,' ',75);fill(w->objects,0,4*sizeof(w->objects[0]));
    w->objects[0].flags=16;w->objects[2].flags=4;w->objects[3].flags=64;
    for(unsigned i=0;i<5;++i) {
        w->maps[25+i]=w->maps[45+i]='#';
        w->maps[25+i*5]=w->maps[29+i*5]='#';
    }
    dok_runtime_actor a={0};a.x=1200;a.z=1200;a.y=-300;a.glyph='M';a.heading=90;
    a.bound_9e=-274;a.bound_a0=-326;w->maps[12]='M';
#define CHECK(test) do {if(!(test)) return __LINE__;} while(0)
    /* Wall at east, exact +1 asymmetry from native side-radius-1. */
    w->maps[25+13]='#';dok_collision_move(&a,30,0,w);CHECK(a.x==1201 && a.z==1200);
    for(unsigned i=0;i<20;++i) dok_collision_move(&a,60,0,w);
    CHECK(a.x==1201 && w->maps[12]=='M');
    /* Four-unit-wide center range between two full-cell walls. */
    w->maps[25+11]='#';dok_collision_move(&a,-60,0,w);CHECK(a.x==1198);
    a.heading=180;dok_collision_move(&a,30,0,w);CHECK(a.z==1230 && a.x==1198);
    /* Adjacent perpendicular wall blocks diagonal corner cutting. */
    w->maps[25+17]='#';a.x=a.z=1200;a.heading=135;
    for(unsigned i=0;i<30;++i) dok_collision_move(&a,60,0,w);
    CHECK(a.x<=1201 && a.z<=1201);
    CHECK(a.y==-300 && a.bound_9e==-274 && a.bound_a0==-326);
    /* Blockers in MMap matter too; FCMap does not participate. */
    fill(w->maps,' ',75);a.x=a.z=1200;a.heading=90;w->maps[12]='M';w->maps[13]='#';
    dok_collision_move(&a,30,0,w);CHECK(a.x==1201);
    w->maps[13]=' ';w->maps[50+13]='#';dok_collision_move(&a,30,0,w);CHECK(a.x==1231);
    /* Door counter balances leaving/re-entering a cell; state is not opened here. */
    w->maps[25+12]='D';w->objects[3].flags=16;w->objects[3].state.door.state_56=1;
    dok_collision_move(&a,-30,0,w);CHECK(w->objects[3].state.door.state_56==1);
    w->objects[3].flags=64;int32_t before=a.x;dok_collision_move(&a,30,0,w);CHECK(a.x==before);
    /* Safe adapter: undefined glyph, open map boundary, huge request. */
    fill(w->maps,' ',75);a.x=238;a.z=1200;w->maps[10]='M';
    dok_collision_move(&a,-30,0,w);CHECK(a.x==238 && w->maps[10]=='M');
    dok_collision_move(&a,32767,0,w);CHECK(a.x==238);
    a.x=a.z=1200;fill(w->maps,' ',75);w->maps[12]='M';w->maps[25+13]='?';
    dok_collision_move(&a,30,0,w);CHECK(a.x==1201);
#undef CHECK
    return 0;
}
