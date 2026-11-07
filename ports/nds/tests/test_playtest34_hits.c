/* Reuse the N17 fixture loader and exclusive scheduler, not its old oracle. */
#include "ai_combat_cases.c"
#include <stdio.h>
#include <stdlib.h>
static dok_ai_combat_cases_workspace space;
static unsigned checks;
#define REQUIRE(e) do { ++checks;if(!(e)){fprintf(stderr,"hits line %d: %s\n",__LINE__,#e);exit(1);} } while(0)
int main(int argc,char **argv)
{
    REQUIRE(argc==2);
    dok_ai_combat_cases_workspace *s=&space;
    dok_combat_cases_workspace *g=&s->simulation.game;
    NdsAssetFile file;dok_asset_reader reader;dok_asset_session session;
    REQUIRE(nds_asset_file_open(&file,argv[1],&reader)==DOK_ASSET_OK);
    REQUIRE(dok_assets_open(&reader,&g->io,&session)==DOK_ASSET_OK);
    unsigned shots=0;
    for(unsigned level=1;level<=3;++level){
        REQUIRE(dok_render_world_load(&session,level,level,&g->world,&g->scratch,g->assets,sizeof(g->assets))==DOK_STATUS_OK);
        uint32_t off=(g->world.asset_bytes+31u)&~31u;
        REQUIRE(dok_sprite_assets_load(&g->world.presentation,&g->bank,g->assets+off,sizeof(g->assets)-off)==DOK_STATUS_OK);
        s->simulation.initial=g->world.initial;
        for(unsigned index=0;index<g->world.initial.object_count;++index){
            if(g->world.initial.objects[index].class_id!=9u||!g->world.initial.objects[index].placements)continue;
            for(unsigned gun=0;gun<2;++gun)for(unsigned angle=0;angle<360;angle+=15)for(int offset=-239;offset<=239;offset+=239){
                REQUIRE(bind(s,index,0,0)==DOK_STATUS_OK);
                dok_ai *ai=&s->simulation.ai;
                for(unsigned j=0;j<ai->count;++j)ai->enemies[j].health=0;
                dok_enemy *e=&ai->enemies[ai->glyph_index[g->world.effective.objects[index].glyph]-1u];
                e->health=1000;e->state=1;e->motion.x=7200+offset;e->motion.z=7200-offset;
                unsigned cell=(unsigned)(e->motion.z/480)*(unsigned)g->world.effective.settings[0]+(unsigned)(e->motion.x/480);
                memset(g->world.initial.maps[DOK_WORLD_MMAP],' ',DOK_LEVEL_MAX_CELLS);
                g->world.initial.maps[DOK_WORLD_MMAP][cell]=e->glyph;
                int32_t sn,cs;REQUIRE(dok_render_trig(angle,&sn,&cs));
                int32_t x=e->motion.x+(int32_t)((int64_t)sn*960/32768);
                int32_t z=e->motion.z-(int32_t)((int64_t)cs*960/32768);
                dok_object_ref shot;
                REQUIRE(dok_combat_spawn(&g->combat,g->world.initial.hero,gun?'2':'1',x,z,(int16_t)angle,&shot)==DOK_STATUS_OK);
                for(unsigned t=416;t<20000&&!s->combat_notices[DOK_COMBAT_IMPACT];t+=416)
                    REQUIRE(sweep(s,100000u+t)==DOK_STATUS_OK&&!g->fault);
                if(s->combat_notices[DOK_COMBAT_IMPACT]!=1||e->health>=1000)
                    fprintf(stderr,"level=%u actor=%u gun=%u angle=%u offset=%d hp=%d impacts=%u misses=%u phase=%u x=%ld z=%ld\n",level,index,gun,angle,offset,e->health,s->combat_notices[2],s->combat_notices[3],g->combat.projectiles[shot.object_id-257u].phase,(long)g->combat.projectiles[shot.object_id-257u].x,(long)g->combat.projectiles[shot.object_id-257u].z);
                REQUIRE(s->combat_notices[DOK_COMBAT_IMPACT]==1&&e->health<1000);
                REQUIRE(dok_combat_clear(&g->combat)==DOK_STATUS_OK);
                REQUIRE(dok_ai_clear(ai)==DOK_STATUS_OK);++shots;
            }
            /* A wall midway along a shot must win over the target. */
            for(unsigned blocked=0;blocked<5;++blocked){
                REQUIRE(bind(s,index,0,0)==DOK_STATUS_OK);dok_ai *ai=&s->simulation.ai;
                for(unsigned j=0;j<ai->count;++j)ai->enemies[j].health=0;
                dok_enemy *e=&ai->enemies[ai->glyph_index[g->world.effective.objects[index].glyph]-1u];
                e->health=blocked==2?0:1000;e->state=1;e->motion.x=7200;e->motion.z=7200;
                unsigned glyph=0,door=0;
                for(unsigned j=0;j<g->world.initial.object_count;++j)if(g->world.effective.objects[j].class_id==2u){glyph=g->world.effective.objects[j].glyph;break;}
                for(unsigned j=0;j<g->world.initial.object_count;++j)if(g->world.effective.objects[j].class_id==7u){door=g->world.effective.objects[j].glyph;break;}
                REQUIRE(glyph!=0&&door!=0);
                if(blocked>=3){
                    g->world.initial.maps[DOK_WORLD_TMAP][13u*(unsigned)g->world.effective.settings[0]+15u]=(unsigned char)door;
                    g->collision.flags[door]=(uint16_t)(blocked==4?8u:0u);
                }
                if(blocked==0)g->world.initial.maps[DOK_WORLD_TMAP][13u*(unsigned)g->world.effective.settings[0]+15u]=(unsigned char)glyph;
                if(blocked==1)e->corpse=1;
                dok_object_ref shot;REQUIRE(dok_combat_spawn(&g->combat,g->world.initial.hero,'1',7200,6000,0,&shot)==DOK_STATUS_OK);
                for(unsigned t=416;t<12000;t+=416)REQUIRE(sweep(s,100000u+t)==DOK_STATUS_OK&&!g->fault);
                REQUIRE(s->combat_notices[DOK_COMBAT_IMPACT]==(blocked==4?1u:0u));
                REQUIRE(blocked==4?e->health<1000:e->health==(blocked==2?0:1000));
                REQUIRE(dok_combat_clear(&g->combat)==DOK_STATUS_OK);REQUIRE(dok_ai_clear(ai)==DOK_STATUS_OK);
            }
        }
    }
    nds_asset_file_close(&file);printf("{\"checks\":%u,\"centered_shots\":%u}\n",checks,shots);return 0;
}
