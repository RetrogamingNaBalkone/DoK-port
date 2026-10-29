#include "collision_cases.h"
#include "dok/input.h"
#include <limits.h>
#include <stddef.h>
#include <string.h>
#define CHECK(r,c) do { ++(r)->checks; if(!(c)){ ++(r)->failures; \
    if((r)->first_failed_line==0u){(r)->first_failed_line=(uint32_t)__LINE__;}return;} } while(0)
static void fixture(dok_collision_test_workspace *w)
{
    static const unsigned char glyphs[6]={32,'H','#','D','M','F'};
    static const uint16_t classes[6]={0,1,2,7,9,12},flags[6]={16,0,2,64,0,16};
    uint32_t i;
    memset(w,0,sizeof(*w));w->definition.language=1;w->definition.object_count=6;
    w->definition.settings[0]=5;w->definition.settings[1]=1;w->definition.settings[2]=5;
    w->definition.settings[3]=480;w->definition.settings[4]=480;w->definition.settings[7]=238;
    w->world.definition=&w->definition;w->world.epoch=7;w->world.object_count=6;
    memset(w->world.maps,32,sizeof(w->world.maps));
    for(i=0;i<6u;++i){dok_world_object_seed *o=&w->world.objects[i];
        w->definition.objects[i].glyph=glyphs[i];w->definition.objects[i].class_id=classes[i];
        w->definition.objects[i].flags=flags[i];w->definition.glyph_index[glyphs[i]]=(uint16_t)(i+1u);
        w->world.glyph_index[glyphs[i]]=(uint16_t)(i+1u);o->definition_index=(uint16_t)i;
        o->class_id=classes[i];o->reference.world_epoch=7;o->reference.object_id=i+1u;o->reference.generation=1;
        o->pose_known=(uint16_t)(classes[i]==1u||classes[i]==9u);o->x=1200;o->y=-300;o->z=1200;
    }
    w->world.hero=w->world.objects[1].reference;w->world.maps[DOK_WORLD_MMAP][12]='H';
}
static dok_motion_state motion(void)
{ dok_motion_state m;(void)dok_motion_init(&m,1200,-300,1200,90,5,120);return m; }
static dok_motion_substep request(int32_t x,int32_t z,int32_t nx,int32_t nz,uint32_t ordinal)
{ dok_motion_substep r={x,-300,z,nx,nz,30,0,ordinal};return r; }
static uint32_t hash_maps(const dok_initial_world *w)
{ uint32_t h=2166136261u,i,j;for(i=0;i<4u;++i)for(j=0;j<4096u;++j)h=(h^w->maps[i][j])*16777619u;return h; }
static void native_cases(dok_collision_test_workspace *w,dok_collision_test_report *t)
{
    dok_collision_view v;uint32_t clear=99,i;int32_t x=11,z=22;
    fixture(w);CHECK(t,dok_collision_bind(&w->collision,&w->world)==DOK_STATUS_OK);
    CHECK(t,dok_collision_get_view(&w->collision,&v)==DOK_STATUS_OK);++t->native_cases;
    CHECK(t,dok_collision_native_clear(&v,1200,1200,238,&clear)==DOK_STATUS_OK&&clear==0u);
    w->world.maps[DOK_WORLD_MMAP][12]=32;
    CHECK(t,dok_collision_native_clear(&v,1200,1200,238,&clear)==DOK_STATUS_OK&&clear==1u);
    w->world.maps[DOK_WORLD_FCMAP][12]='#';
    CHECK(t,dok_collision_native_clear(&v,1200,1200,238,&clear)==DOK_STATUS_OK&&clear==1u);
    for(i=0;i<2u;++i){w->world.maps[i][13]='#';
        CHECK(t,dok_collision_native_resolve(&v,1200,1200,1300,1200,&x,&z)==DOK_STATUS_OK);
        CHECK(t,x==1201&&z==1200);
        CHECK(t,dok_collision_native_resolve(&v,1200,1200,1300,1250,&x,&z)==DOK_STATUS_OK);
        CHECK(t,x==1201&&z==1250);w->world.maps[i][13]=32;++t->native_cases;
    }
    w->world.maps[DOK_WORLD_MMAP][12]=255;
    CHECK(t,dok_collision_native_clear(&v,1200,1200,238,&clear)==DOK_STATUS_OK&&clear==0u);
    w->world.maps[DOK_WORLD_MMAP][12]=32;
    CHECK(t,dok_collision_native_clear(&v,-100,-100,238,&clear)==DOK_STATUS_OK&&clear==1u);
    CHECK(t,dok_collision_native_clear(&v,INT32_MAX,INT32_MIN,238,&clear)==DOK_STATUS_OK&&clear==1u);
    v.cell_side=0;clear=99;
    CHECK(t,dok_collision_native_clear(&v,0,0,238,&clear)==DOK_STATUS_INVALID_ARGUMENT&&clear==99u);
    CHECK(t,dok_collision_native_resolve(&v,0,0,1,1,&x,&z)==DOK_STATUS_INVALID_ARGUMENT&&x==1201&&z==1250);
}
static void transactions(dok_collision_test_workspace *w,dok_collision_test_report *t)
{
    dok_motion_state m=motion();dok_motion_substep r;dok_collision_plan p;dok_collision_report report;
    dok_object_ref bad;uint32_t h;int32_t x,z;
    fixture(w);CHECK(t,dok_collision_bind(&w->collision,&w->world)==DOK_STATUS_OK);
    bad=w->world.hero;++bad.generation;
    CHECK(t,dok_collision_begin(&w->collision,bad,&m,416,&w->context)==DOK_STATUS_STALE_REFERENCE);
    bad=w->world.hero;bad.object_id=0;
    CHECK(t,dok_collision_begin(&w->collision,bad,&m,416,&w->context)==DOK_STATUS_STALE_REFERENCE);
    bad=w->world.hero;++bad.world_epoch;
    CHECK(t,dok_collision_begin(&w->collision,bad,&m,416,&w->context)==DOK_STATUS_STALE_REFERENCE);
    m.speed=4000;m.movement_accumulator=10000;m.flags=DOK_INPUT_NATIVE_FORWARD;
    CHECK(t,dok_collision_begin(&w->collision,w->world.hero,&m,416,&w->context)==DOK_STATUS_LIMIT_REACHED);
    m=motion();w->world.maps[DOK_WORLD_MMAP][12]='M';
    CHECK(t,dok_collision_begin(&w->collision,w->world.hero,&m,416,&w->context)==DOK_STATUS_INVALID_STATE);
    w->world.maps[DOK_WORLD_MMAP][12]='H';w->world.maps[DOK_WORLD_TMAP][12]='D';
    CHECK(t,dok_collision_set_flags(&w->collision,w->world.objects[3].reference,80)==DOK_STATUS_OK);
    CHECK(t,dok_collision_begin(&w->collision,w->world.hero,&m,416,&w->context)==DOK_STATUS_OK);
    CHECK(t,dok_collision_set_flags(&w->collision,w->world.objects[3].reference,64)==DOK_STATUS_INVALID_STATE);
    r=request(1200,1200,1200,1200,0);h=hash_maps(&w->world);
    CHECK(t,dok_collision_query(&w->context,&r,&p)==DOK_STATUS_OK);
    CHECK(t,hash_maps(&w->world)==h&&w->collision.door_count['D']==0u);
    CHECK(t,dok_collision_commit(&w->context,&p)==DOK_STATUS_OK&&w->collision.door_count['D']==1u);
    CHECK(t,dok_collision_commit(&w->context,&p)==DOK_STATUS_INVALID_STATE);
    r.ordinal=1;dok_collision_move(&w->context,&r,&x,&z);
    CHECK(t,x==1200&&z==1200&&w->collision.door_count['D']==1u);
    r.ordinal=2;dok_collision_move(&w->context,&r,&x,&z);
    CHECK(t,w->collision.door_count['D']==1u);
    CHECK(t,dok_collision_end(&w->context,&report)==DOK_STATUS_OK);
    CHECK(t,report.requested==3u&&report.committed==3u&&report.rejected==0u);++t->transaction_cases;
    CHECK(t,dok_collision_set_door_count(&w->collision,w->world.objects[3].reference,65535u)==DOK_STATUS_OK);
    /* Entering a new door cell increments a WORD counter modulo65536. */
    w->world.maps[DOK_WORLD_TMAP][12]=32;w->world.maps[DOK_WORLD_TMAP][13]='D';
    m.x=1430;CHECK(t,dok_collision_begin(&w->collision,w->world.hero,&m,416,&w->context)==DOK_STATUS_OK);
    r=request(1430,1200,1450,1200,0);dok_collision_move(&w->context,&r,&x,&z);
    CHECK(t,x==1450&&z==1200&&w->collision.door_count['D']==0u);
    CHECK(t,w->world.maps[DOK_WORLD_MMAP][12]==32u&&w->world.maps[DOK_WORLD_MMAP][13]=='H');
    CHECK(t,dok_collision_end(&w->context,&report)==DOK_STATUS_OK);++t->transaction_cases;
    /* Full open-edge source clamp is deliberately bounded after resolve. */
    fixture(w);CHECK(t,dok_collision_bind(&w->collision,&w->world)==DOK_STATUS_OK);
    w->world.maps[DOK_WORLD_MMAP][12]=32;w->world.maps[DOK_WORLD_MMAP][10]='H';m=motion();m.x=238;
    CHECK(t,dok_collision_begin(&w->collision,w->world.hero,&m,416,&w->context)==DOK_STATUS_OK);
    r=request(238,1200,208,1200,0);dok_collision_move(&w->context,&r,&x,&z);
    CHECK(t,x==238&&z==1200);
    CHECK(t,dok_collision_end(&w->context,&report)==DOK_STATUS_OK);
    CHECK(t,report.rejected==1u&&report.reason_mask==DOK_COLLISION_WORLD_EDGE&&report.committed==1u);
    CHECK(t,w->world.maps[DOK_WORLD_MMAP][10]=='H');++t->transaction_cases;
    /* A slightly outside raw candidate can still slide along a closed edge. */
    w->world.maps[DOK_WORLD_MMAP][10]=32;w->world.maps[DOK_WORLD_MMAP][11]='H';
    w->world.maps[DOK_WORLD_TMAP][10]='#';m.x=718;
    CHECK(t,dok_collision_begin(&w->collision,w->world.hero,&m,416,&w->context)==DOK_STATUS_OK);
    r=request(718,1200,688,1250,0);dok_collision_move(&w->context,&r,&x,&z);
    CHECK(t,x==718&&z==1250);
    CHECK(t,dok_collision_end(&w->context,&report)==DOK_STATUS_OK);
    CHECK(t,report.adjusted==1u&&report.rejected==0u);++t->transaction_cases;
    /* Large individual request is bounded before resolver, but still performs
     * the source same-cell occupancy/counter bookkeeping. */
    fixture(w);CHECK(t,dok_collision_bind(&w->collision,&w->world)==DOK_STATUS_OK);
    m.x=-1;CHECK(t,dok_collision_begin(&w->collision,w->world.hero,&m,416,&w->context)==DOK_STATUS_INVALID_STATE);
    m=motion();CHECK(t,dok_collision_begin(&w->collision,w->world.hero,&m,416,&w->context)==DOK_STATUS_OK);
    r=request(1200,1200,1500,1200,0);dok_collision_move(&w->context,&r,&x,&z);
    CHECK(t,x==1200&&z==1200);
    CHECK(t,dok_collision_end(&w->context,&report)==DOK_STATUS_OK);
    CHECK(t,report.rejected==1u&&report.reason_mask==DOK_COLLISION_LARGE_STEP&&report.committed==1u);
    CHECK(t,dok_collision_set_door_count(&w->collision,w->world.hero,1)==DOK_STATUS_INVALID_ARGUMENT);
    ++t->transaction_cases;
    /* Epoch reuse and revision exhaustion are admitted nowhere. */
    w->collision._revision=UINT32_MAX-11u;
    CHECK(t,dok_collision_begin(&w->collision,w->world.hero,&m,416,&w->context)==DOK_STATUS_LIMIT_REACHED);
    w->collision._revision=UINT32_MAX;
    CHECK(t,dok_collision_set_flags(&w->collision,w->world.hero,0)==DOK_STATUS_LIMIT_REACHED);
    ++w->world.epoch;
    CHECK(t,dok_collision_begin(&w->collision,w->world.hero,&m,416,&w->context)==DOK_STATUS_INVALID_STATE);
}
static void cameras(dok_collision_test_workspace *w,dok_collision_test_report *t)
{
    uint32_t clear,h;uint16_t counter;dok_motion_state m=motion();dok_collision_report r;
    fixture(w);CHECK(t,dok_collision_bind(&w->collision,&w->world)==DOK_STATUS_OK);
    h=hash_maps(&w->world);counter=w->collision.door_count['D'];
    CHECK(t,dok_collision_camera_clear(&w->collision,w->world.hero,1200,1200,1200,1200,&clear)==DOK_STATUS_OK&&clear==1u);
    w->world.maps[DOK_WORLD_MMAP][13]='H';
    CHECK(t,dok_collision_camera_clear(&w->collision,w->world.hero,1200,1200,1300,1200,&clear)==DOK_STATUS_OK&&clear==0u);
    w->world.maps[DOK_WORLD_MMAP][13]=32;
    CHECK(t,dok_collision_camera_clear(&w->collision,w->world.hero,1200,1200,-1,1200,&clear)==DOK_STATUS_OK&&clear==0u);
    CHECK(t,hash_maps(&w->world)==h&&w->collision.door_count['D']==counter);
    CHECK(t,dok_collision_begin(&w->collision,w->world.hero,&m,416,&w->context)==DOK_STATUS_OK);
    clear=99;CHECK(t,dok_collision_camera_clear(&w->collision,w->world.hero,1200,1200,1200,1200,&clear)==DOK_STATUS_INVALID_STATE&&clear==99u);
    CHECK(t,dok_collision_end(&w->context,&r)==DOK_STATUS_OK);++t->camera_cases;
}
static void corridors(dok_collision_test_workspace *w,dok_collision_test_report *t)
{
    dok_collision_view v;uint32_t i,clear;int32_t x,z;
    fixture(w);w->world.maps[DOK_WORLD_MMAP][12]=32;
    for(i=0;i<5u;++i){w->world.maps[DOK_WORLD_TMAP][i*5u]='#';w->world.maps[DOK_WORLD_TMAP][i*5u+2u]='#';}
    CHECK(t,dok_collision_bind(&w->collision,&w->world)==DOK_STATUS_OK);
    CHECK(t,dok_collision_get_view(&w->collision,&v)==DOK_STATUS_OK);
    for(i=717;i<=722u;++i){
        CHECK(t,dok_collision_native_clear(&v,(int32_t)i,1200,238,&clear)==DOK_STATUS_OK);
        CHECK(t,clear==(uint32_t)(i>=718u&&i<=721u));++t->native_cases;
    }
    for(i=0;i<8u;++i){int32_t dx=(i&1u)!=0u?100:-100,dz=(i&2u)!=0u?50:-50;
        if((i&4u)!=0u){dx=dx<0?-30:30;dz=dz<0?-30:30;}
        CHECK(t,dok_collision_native_resolve(&v,720,1200,720+dx,1200+dz,&x,&z)==DOK_STATUS_OK);
        CHECK(t,x==(dx<0?718:721)&&z==1200+dz);++t->native_cases;
    }
}
static void doors(dok_collision_test_workspace *w,dok_collision_test_report *t)
{
    dok_motion_state m=motion();dok_motion_substep step;dok_collision_report report;int32_t x,z;
    fixture(w);w->world.maps[DOK_WORLD_TMAP][13]='D';
    CHECK(t,dok_collision_bind(&w->collision,&w->world)==DOK_STATUS_OK);
    CHECK(t,dok_collision_begin(&w->collision,w->world.hero,&m,416,&w->context)==DOK_STATUS_OK);
    step=request(1200,1200,1300,1200,0);dok_collision_move(&w->context,&step,&x,&z);
    CHECK(t,x==1201&&z==1200);
    CHECK(t,dok_collision_end(&w->context,&report)==DOK_STATUS_OK&&report.adjusted==1u);
    CHECK(t,dok_collision_set_flags(&w->collision,w->world.objects[3].reference,80)==DOK_STATUS_OK);
    /* Y/bounds and FCMap do not participate in this original horizontal path. */
    m.x=x;m.y=-900;w->world.objects[1].upper_y=200;w->world.objects[1].lower_y=-1000;
    w->world.maps[DOK_WORLD_FCMAP][12]='#';
    CHECK(t,dok_collision_begin(&w->collision,w->world.hero,&m,416,&w->context)==DOK_STATUS_OK);
    step=request(1201,1200,1300,1200,0);step.y=-900;dok_collision_move(&w->context,&step,&x,&z);
    CHECK(t,x==1300&&z==1200);
    CHECK(t,dok_collision_end(&w->context,&report)==DOK_STATUS_OK&&report.adjusted==0u);
    CHECK(t,m.y==-900&&w->world.objects[1].upper_y==200&&w->world.objects[1].lower_y==-1000);
    ++t->transaction_cases;
}
int dok_collision_run_cases(dok_collision_test_workspace *w,dok_collision_test_report *t)
{
    if(w==NULL||t==NULL)return 1;
    memset(t,0,sizeof(*t));native_cases(w,t);transactions(w,t);cameras(w,t);corridors(w,t);doors(w,t);return t->failures==0u?0:1;
}
