#include "dok/gameflow.h"
#include "dok/frontend.h"
#include "gameplay.h"
#include "effects_runtime.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned long checks;
#define CHECK(e) do{++checks;if(!(e)){fprintf(stderr,"flow check line %d: %s\n",__LINE__,#e);exit(1);}}while(0)
static NdsGameplay game,before,expected;
static dok_level_definition definition;
static dok_initial_world world,world_expected;
static dok_event_scheduler scheduler,scheduler_before;
static dok_event_bank bank,bank_before;
static uint32_t ticks=100;
static dok_status read_ticks(void *p,uint32_t *out){*out=*(uint32_t *)p;return DOK_STATUS_OK;}
static void callback(void *p,struct dok_event_scheduler *s,dok_object_ref r,uint16_t c,uint32_t v)
{(void)p;(void)s;(void)r;(void)c;(void)v;abort();}
/* Retry must never request render/effect preparation. Unused gameplay entry
 * points link their real core dependencies; crossing this platform boundary
 * in the tested retry path is an immediate test failure. */
dok_status nds_effects_arm_bounds(NdsEffectsRuntime *r,const dok_render_gather_bounds *b,const dok_sprite_list *s)
{(void)r;(void)b;(void)s;abort();}
static int16_t lowword(int32_t n)
{uint32_t u=(uint32_t)n&65535u;return (int16_t)(u<32768u?(int32_t)u:(int32_t)u-65536);}
static uint32_t cell(int32_t x,int32_t z)
{
    int32_t a=lowword(x/480),b=lowword(z/480);
    a=a<0?0:a>63?63:a;b=b<0?0:b>63?63:b;return (uint32_t)(b*64+a);
}
static void life_domain(void)
{
    /* Entire WORD domain and both signed HP branches; oracle uses an
     * independent signed mathematical wrap, not implementation expressions. */
    for(int32_t n=INT16_MIN;n<=INT16_MAX;++n){
        int16_t lives=(int16_t)n;
        int32_t after=n==INT16_MIN?INT16_MAX:n-1;
        CHECK(dok_gameflow_check(0,&lives)==(after==0?DOK_FLOW_GAMEOVER:DOK_FLOW_RETRY));
        CHECK(lives==after);
        for(unsigned j=0;j<4u;++j){
            const int16_t health[4]={INT16_MIN,-1,1,INT16_MAX};lives=(int16_t)n;
            CHECK(dok_gameflow_check(health[j],&lives)==(health[j]<0?DOK_FLOW_FINISH:DOK_FLOW_ALIVE));
            CHECK(lives==n);
        }
    }
}
static void motion_fields(void)
{
    dok_motion_state s,spawn,want;
    for(unsigned iteration=0;iteration<256u;++iteration){
        /* Fill every modeled native field and padding with disjoint sentinels.
         * Whole-object comparison detects accidental reset of inertia/history. */
        memset(&s,(int)iteration,sizeof(s));memset(&spawn,(int)(iteration^0xa5u),sizeof(spawn));want=s;
        want.x=spawn.x;want.y=spawn.y;want.z=spawn.z;
        want.heading=spawn.heading;want.angle_parameter=spawn.angle_parameter;
        want.speed=spawn.speed;want.distance=spawn.distance;
        dok_gameflow_restore_motion(&s,&spawn);CHECK(memcmp(&s,&want,sizeof(s))==0);
    }
}
static void retry_integration(void)
{
    const int32_t positions[][4]={
        {960,960,480,480},{480,480,480,480},{-960,99999,99999,-960},
        {-479,-479,0,0},{480*65536+480,480*65536+960,480,960},
        {INT32_MAX,INT32_MIN,480*32768,480*65535},
        {0,0,INT32_MIN,INT32_MAX},{63*480,63*480,0,0},
    };
    memset(&definition,0,sizeof(definition));
    definition.settings[DOK_LEVEL_WIDTH]=64;definition.settings[DOK_LEVEL_LENGTH]=64;
    definition.settings[DOK_LEVEL_HEIGHT]=1;definition.settings[DOK_LEVEL_MIN_DISTANCE]=238;
    definition.settings[DOK_LEVEL_CELL_SIDE]=480;definition.language=1;definition.object_count=3;
    definition.objects[0].glyph='@';definition.objects[0].class_id=1;
    definition.objects[1].glyph=' ';definition.objects[1].class_id=0;definition.objects[1].flags=16;
    definition.objects[2].glyph='A';definition.objects[2].class_id=9;
    definition.glyph_index['@']=1;definition.glyph_index[' ']=2;definition.glyph_index['A']=3;
    for(unsigned iteration=0;iteration<sizeof(positions)/sizeof(positions[0]);++iteration){
        memset(&game,0x53,sizeof(game));memset(&world,0x67,sizeof(world));
        world.definition=&definition;world.hero=(dok_object_ref){42,1,1};world.epoch=42;world.object_count=3;
        world.glyph_index['@']=1;world.glyph_index[' ']=2;world.glyph_index['A']=3;
        for(unsigned i=0;i<3u;++i){world.objects[i].definition_index=(uint16_t)i;
            world.objects[i].class_id=definition.objects[i].class_id;
            world.objects[i].reference=(dok_object_ref){42,i+1u,1};world.objects[i].pose_known=1;}
        world.objects[2].x=positions[iteration][2];world.objects[2].z=positions[iteration][3];
        memset(world.maps,' ',3u*DOK_LEVEL_MAX_CELLS);
        world.maps[DOK_WORLD_MMAP][cell(positions[iteration][2],positions[iteration][3])]='A';
        CHECK(dok_collision_bind(&game.collision,&world)==DOK_STATUS_OK);
        dok_event_config config={&ticks,read_ticks,0,callback,42};
        CHECK(dok_events_init(&scheduler,&bank,&config)==DOK_STATUS_OK);
        CHECK(dok_events_register(&scheduler,world.hero,4)==DOK_STATUS_OK);
        CHECK(dok_events_schedule(&scheduler,world.hero,0x1701,0x12345678,416)==DOK_STATUS_OK);
        dok_object_ref missile={42,0x10001,7};
        CHECK(dok_events_register(&scheduler,missile,4)==DOK_STATUS_OK);
        CHECK(dok_events_schedule(&scheduler,missile,DOK_COMBAT_PROJECTILE,99,207)==DOK_STATUS_OK);
        game.items.world=&world;game.items.collision=&game.collision;
        game.items.inventory.health=0;game.items.inventory.max_health=iteration&1u?-123:111;
        game.collision._active=0;game.collision._world=&world;
        game.combat.items=&game.items;game.combat.scheduler=&scheduler;
        game.ai.world=&world;game.ai.scheduler=&scheduler;game.ai.combat=&game.combat;
        game.hero.x=positions[iteration][0];game.hero.z=positions[iteration][1];
        game.spawn.x=positions[iteration][2];game.spawn.z=positions[iteration][3];
        memset(&game.spawn.y,0xa4,sizeof(game.spawn.y));
        before=game;expected=game;world_expected=world;scheduler_before=scheduler;bank_before=bank;
        expected.hero.x=game.spawn.x;expected.hero.y=game.spawn.y;expected.hero.z=game.spawn.z;
        expected.hero.heading=game.spawn.heading;expected.hero.angle_parameter=game.spawn.angle_parameter;
        expected.hero.speed=game.spawn.speed;expected.hero.distance=game.spawn.distance;
        expected.items.inventory.health=game.items.inventory.max_health;
        expected.collision._respawn_cell=cell(game.spawn.x,game.spawn.z);
        memset(expected.collision._respawn_origins,0,sizeof(expected.collision._respawn_origins));
        expected.collision._respawn_origins[0]=5u; /* Exact hero + class9 seed identities. */
        ++expected.items.revision;++expected.collision._revision;++expected.retries;
        world_expected.maps[DOK_WORLD_MMAP][cell(game.hero.x,game.hero.z)]=32;
        world_expected.maps[DOK_WORLD_MMAP][cell(game.spawn.x,game.spawn.z)]='@';
        world_expected.objects[0].x=game.spawn.x;world_expected.objects[0].y=game.spawn.y;
        world_expected.objects[0].z=game.spawn.z;world_expected.objects[0].heading=game.spawn.heading;
        CHECK(nds_gameplay_retry(&game)==DOK_STATUS_OK);
        CHECK(memcmp(&game,&expected,sizeof(game))==0);
        CHECK(memcmp(&world,&world_expected,sizeof(world))==0);
        CHECK(memcmp(&scheduler,&scheduler_before,sizeof(scheduler))==0);
        CHECK(memcmp(&bank,&bank_before,sizeof(bank))==0);
        /* A second retry with a live/sentinel hero must refuse atomically. */
        before=game;CHECK(nds_gameplay_retry(&game)==DOK_STATUS_INVALID_STATE);
        CHECK(memcmp(&game,&before,sizeof(game))==0);
        CHECK(memcmp(&world,&world_expected,sizeof(world))==0);
        game.items.inventory.health=0;game.collision._active=(dok_collision_context *)&scheduler;
        before=game;CHECK(nds_gameplay_retry(&game)==DOK_STATUS_INVALID_STATE);
        CHECK(memcmp(&game,&before,sizeof(game))==0);
        CHECK(memcmp(&world,&world_expected,sizeof(world))==0);
    }
    CHECK(nds_gameplay_retry(0)==DOK_STATUS_INVALID_STATE);
}
static void frontend_transitions(void)
{
    const uint16_t pages[2][8]={{3,2,1,2,1,2,2,3},{3,3,1,2,1,3,2,3}};
    for(uint8_t language=1;language<=2;++language)for(uint8_t level=1;level<=3;++level){
        for(unsigned paused=0;paused<2u;++paused){
            dok_frontend s;dok_frontend_init(&s,language,pages);
            s.level=level;s.state=paused?DOK_FRONTEND_PAUSE:DOK_FRONTEND_PLAY;
            s.previous_keys=DOK_FRONTEND_KEY_A;s.page=2;s.page_count=3;
            uint32_t result=dok_frontend_game_over(&s);
            CHECK(result==(DOK_FRONTEND_STOP_GAME|(paused?0u:DOK_FRONTEND_PAUSE_GAME)));
            CHECK(s.state==DOK_FRONTEND_LEVELS&&s.selection==level-1u&&s.language==language);
            CHECK(s.blocked&&s.page==0&&s.page_count==1);
            CHECK(dok_frontend_step(&s,DOK_FRONTEND_KEY_A,0,1)==0);
            CHECK(s.state==DOK_FRONTEND_LEVELS);
            CHECK(dok_frontend_step(&s,0,0,1)==0);
            CHECK(!s.blocked);
            CHECK(dok_frontend_step(&s,DOK_FRONTEND_KEY_A,0,1)==0);
            CHECK(s.state==DOK_FRONTEND_INTRO&&s.level==level&&s.page_count==pages[language-1u][2u*level-1u]);
            unsigned count=s.page_count;
            for(unsigned page=0;page<count;++page){
                CHECK(dok_frontend_step(&s,0,0,1)==0);
                CHECK(dok_frontend_step(&s,DOK_FRONTEND_KEY_A,0,1)==(page+1u==count?DOK_FRONTEND_NEW_LEVEL:0u));
            }
            CHECK(s.state==DOK_FRONTEND_PLAY&&s.blocked);
        }
    }
}
int main(void)
{
    life_domain();motion_fields();retry_integration();frontend_transitions();
    printf("{\"passed\":true,\"checks\":%lu,\"life_domain\":65536,\"integration_vectors\":8,\"frontend_branches\":12}\n",checks);return 0;
}
