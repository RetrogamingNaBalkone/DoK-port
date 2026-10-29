/* N11 actual-source world integration. Timelines and inventories are fixtures. */
#include "dok/interactions.h"
#include "dok/interactions_move.h"
#include "dok/render_materials.h"
#include "dok/render_scene.h"
#include "file_reader.h"
#include <stdio.h>
#include <string.h>

static dok_render_world render;
static dok_render_workspace loader;
static dok_level_definition localized;
static dok_level_presentation en_presentation;
static dok_asset_workspace catalog;
static dok_asset_session session;
static unsigned char assets[DOK_NDS_ASSETS_BYTES], en_bytes[65536];
static dok_initial_world baseline_world;
static dok_initial_world conveyor_world;
static dok_collision_state collision;
static dok_collision_state conveyor_collision;
static dok_event_bank bank;
static dok_event_scheduler scheduler;
static dok_interactions_state interactions;
static dok_interactions_state conveyor_interactions;
static dok_interaction_phase_view phases;
static uint32_t now, c2, checks, failures, outputs[7], callback_count, error;
static uint32_t observed, view_hash, conveyor_cases, conveyor_guards;
#define CHECK(v) do { ++checks; if (!(v)) { ++failures; \
    fprintf(stderr,"N11 world line %d\n",__LINE__); return 1; } } while (0)
static dok_status read_clock(void *context,uint32_t *out)
{ (void)context; *out=now; return DOK_STATUS_OK; }
static int trig(void *context,uint32_t heading,int32_t *s,int32_t *c)
{ (void)context; return dok_render_trig(heading,s,c); }
static void sink(void *context,const dok_interaction_output *out)
{ (void)context; if(out->kind<7u)++outputs[out->kind];else error=1; }
static void callback(void *context,dok_event_scheduler *s,dok_object_ref owner,
    uint16_t id,uint32_t parameter)
{
    (void)context;(void)s;(void)parameter;++callback_count;
    if(dok_interactions_callback(&interactions,owner,id)!=DOK_STATUS_OK)error=1;
}
static uint32_t hash_bytes(uint32_t h,const void *data,size_t n)
{ const unsigned char *p=data;size_t i;for(i=0;i<n;++i)h=(h^p[i])*16777619u;return h; }
static uint32_t state_hash(void)
{
    uint32_t h=2166136261u,i;
    h=hash_bytes(h,render.initial.maps,sizeof(render.initial.maps));
    h=hash_bytes(h,collision.flags,sizeof(collision.flags));
    h=hash_bytes(h,collision.door_count,sizeof(collision.door_count));
    h=hash_bytes(h,interactions.doors,sizeof(interactions.doors));
    h=hash_bytes(h,&interactions.previous_floor,sizeof(interactions.previous_floor));
    h=hash_bytes(h,outputs,sizeof(outputs));
    h=hash_bytes(h,&callback_count,sizeof(callback_count));
    h=hash_bytes(h,&now,sizeof(now));h=hash_bytes(h,&c2,sizeof(c2));
    h=hash_bytes(h,&collision._revision,sizeof(collision._revision));
    h=hash_bytes(h,&interactions.revision,sizeof(interactions.revision));
    h=hash_bytes(h,&interactions.schedule_refusals,sizeof(interactions.schedule_refusals));
    h=hash_bytes(h,&interactions.fault,sizeof(interactions.fault));
    h=hash_bytes(h,&scheduler._event_count,sizeof(scheduler._event_count));
    h=hash_bytes(h,&scheduler._registered_count,sizeof(scheduler._registered_count));
    h=hash_bytes(h,&scheduler._owner_count,sizeof(scheduler._owner_count));
    h=hash_bytes(h,&scheduler._cursor,sizeof(scheduler._cursor));
    h=hash_bytes(h,&scheduler._pending_head,sizeof(scheduler._pending_head));
    h=hash_bytes(h,&scheduler._pending_tail,sizeof(scheduler._pending_tail));
    h=hash_bytes(h,&scheduler._fault,sizeof(scheduler._fault));
    for(i=0;i<render.initial.object_count;++i)if(render.initial.objects[i].class_id==7u){
        const dok_event_owner *owner=NULL;
        if(dok_events_inspect_owner(&scheduler,render.initial.objects[i].reference,&owner)!=DOK_STATUS_OK)
            return 0;
        h=hash_bytes(h,&owner->count,sizeof(owner->count));
        h=hash_bytes(h,owner->events,owner->count*sizeof(owner->events[0]));
    }
    return h;
}
static int observation(void)
{
    uint32_t before=state_hash();
    CHECK(dok_interactions_phases(&interactions,&phases)==DOK_STATUS_OK);
    CHECK(phases.world_epoch==render.initial.epoch&&phases.revision==interactions.revision);
    CHECK(before==state_hash());
    view_hash=hash_bytes(view_hash,&phases,sizeof(phases));++observed;
    return 0;
}
static int conveyors(void)
{
    static const uint8_t glyphs[4]={'<','>','{','}'};
    static const uint32_t divisors[4]={4,5,6,60};
    uint32_t direction,d,cell,j,found=0,hero_glyph;
    unsigned char native_mmap[4096];
    conveyor_cases=conveyor_guards=0;
    conveyor_world=render.initial;conveyor_collision=collision;conveyor_interactions=interactions;
    hero_glyph=0;
    /* IDs are not glyphs or guaranteed seed indexes. Resolve the full hero ref. */
    for(j=0;j<render.initial.object_count;++j)
        if(render.initial.objects[j].reference.object_id==render.initial.hero.object_id&&
           render.initial.objects[j].reference.generation==render.initial.hero.generation&&
           render.initial.objects[j].reference.world_epoch==render.initial.hero.world_epoch){
            hero_glyph=render.effective.objects[render.initial.objects[j].definition_index].glyph;break;
        }
    CHECK(j<render.initial.object_count);
    for(direction=0;direction<4u;++direction){
        uint16_t definition_index=render.initial.glyph_index[glyphs[direction]];
        const dok_level_object *object;
        const dok_level_field *category,*value;
        if(definition_index==0u)continue;
        object=&render.effective.objects[definition_index-1u];
        if(object->class_id!=12u)continue;
        for(cell=0;cell<4096u;++cell)
            if(conveyor_world.maps[DOK_WORLD_FCMAP][cell]==glyphs[direction])break;
        if(cell==4096u)continue; /* source includes unplaced declarations */
        category=dok_level_field_at(object,0x66);value=dok_level_field_at(object,0x68);
        CHECK(category!=NULL&&value!=NULL&&(category->value==100||category->value==102));
        for(cell=0;cell<4096u;++cell)
            if(conveyor_world.maps[DOK_WORLD_FCMAP][cell]==glyphs[direction]&&
               conveyor_world.maps[DOK_WORLD_MMAP][cell]==32u&&
               (conveyor_collision.flags[conveyor_world.maps[DOK_WORLD_TMAP][cell]]&16u)!=0u)break;
        CHECK(cell<4096u);++found;
        for(d=0;d<4u;++d){
            dok_motion_state motion,before;
            dok_collision_view native;
            int32_t x=(int32_t)(cell%64u)*480+240,z=(int32_t)(cell/64u)*480+240,nx,nz,dx;
            uint32_t clear=0,new_cell,guard;
            render.initial=conveyor_world;collision=conveyor_collision;interactions=conveyor_interactions;
            /* Explicit relocation fixture: remove only this hero glyph; retain
             * every other actor and actual walls/doors/floor declarations. */
            for(j=0;j<4096u;++j)if(render.initial.maps[DOK_WORLD_MMAP][j]==hero_glyph)
                render.initial.maps[DOK_WORLD_MMAP][j]=32;
            render.initial.maps[DOK_WORLD_MMAP][cell]=(unsigned char)hero_glyph;
            CHECK(dok_motion_init(&motion,x,-300,z,75,5,120)==DOK_STATUS_OK);
            motion.movement_accumulator=731;motion.turn_accumulator=-19;
            before=motion;dx=(int16_t)value->value/(int32_t)divisors[d];
            nx=x+(category->value==100?dx:0);nz=z+(category->value==102?dx:0);
            CHECK(dok_collision_get_view(&collision,&native)==DOK_STATUS_OK);
            memcpy(native_mmap,native.mmap,sizeof(native_mmap));native_mmap[cell]=32;
            native.mmap=native_mmap;
            CHECK(dok_collision_native_resolve(&native,x,z,nx,nz,&nx,&nz)==DOK_STATUS_OK);
            CHECK(dok_collision_native_clear(&native,nx,nz,238,&clear)==DOK_STATUS_OK);
            guard=nx<238||nz<238||nx+238>=30720||nz+238>=30720||clear==0u;
            if(guard){nx=x;nz=z;++conveyor_guards;}
            CHECK(dok_interactions_floor(&interactions,render.initial.hero,&motion,0,divisors[d])==DOK_STATUS_OK);
            CHECK(motion.x==nx&&motion.z==nz&&(motion.flags&DOK_MOTION_SLOW)!=0u);
            new_cell=(uint32_t)(nz/480)*64u+(uint32_t)(nx/480);
            native_mmap[new_cell]=(unsigned char)hero_glyph;
            CHECK(memcmp(render.initial.maps[DOK_WORLD_MMAP],native_mmap,sizeof(native_mmap))==0);
            for(j=1;j<4u;++j)CHECK(memcmp(render.initial.maps[j],conveyor_world.maps[j],4096)==0);
            CHECK(collision._revision==conveyor_collision._revision+1u);
            motion.x=before.x;motion.z=before.z;motion.flags=before.flags;
            CHECK(memcmp(&motion,&before,sizeof(motion))==0);++conveyor_cases;
        }
    }
    render.initial=conveyor_world;collision=conveyor_collision;interactions=conveyor_interactions;
    CHECK((render.effective.source_index==19u&&found==4u&&conveyor_cases==16u)||
          (render.effective.source_index!=19u&&found==0u));
    return 0;
}
static int run(uint32_t hz,uint32_t *result,uint32_t *callbacks,uint32_t *door_count)
{
    dok_event_config config;
    dok_interaction_services services;
    dok_event_dispatch_report dispatch;
    uint32_t i,frame,door_total=0,lock_total=0,closed=0,opened=0,blocked_glyph=256;
    memset(&collision,0,sizeof(collision));render.initial=baseline_world;
    CHECK(dok_collision_bind(&collision,&render.initial)==DOK_STATUS_OK);
    memset(&config,0,sizeof(config));config.read_ticks=read_clock;config.callback=callback;
    config.world_epoch=render.initial.epoch;
    CHECK(dok_events_init(&scheduler,&bank,&config)==DOK_STATUS_OK);
    memset(&services,0,sizeof(services));services.trig=trig;services.sink=sink;
    services.move=dok_interactions_move;services.move_context=&collision;
    CHECK(dok_interactions_bind(&interactions,&render.initial,&collision,&scheduler,&services)==DOK_STATUS_OK);
    CHECK(dok_interactions_register(&interactions)==DOK_STATUS_OK);
    memset(outputs,0,sizeof(outputs));callback_count=error=observed=0;now=0;c2=4;view_hash=2166136261u;
    for(i=0;i<render.initial.object_count;++i){
        const dok_world_object_seed *seed=&render.initial.objects[i];
        const dok_level_object *o=&render.effective.objects[seed->definition_index];
        uint32_t bits=0x800u;uint32_t before;
        if(o->class_id==7u){
            const dok_level_field *key=dok_level_field_at(o,0x5c);
            ++door_total;
            if(key!=NULL&&key->value!=0){
                uint32_t locked=outputs[DOK_INTERACTION_LOCKED];
                CHECK(dok_interactions_event(&interactions,seed->reference,render.initial.hero,&bits,0,60)==DOK_STATUS_OK);
                CHECK(bits==0u&&outputs[DOK_INTERACTION_LOCKED]==locked+1u);
                CHECK(interactions.doors[o->glyph].phase==0&&interactions.doors[o->glyph].ready==1u);
                ++lock_total;bits=0x800u;
            }
            CHECK(dok_interactions_event(&interactions,seed->reference,render.initial.hero,&bits,UINT16_MAX,60)==DOK_STATUS_OK);
            CHECK(bits==0u);
            if(blocked_glyph==256u)blocked_glyph=o->glyph;
        }else if(o->class_id==6u){
            before=state_hash();
            CHECK(dok_interactions_event(&interactions,seed->reference,render.initial.hero,&bits,0,60)==DOK_STATUS_OK);
            CHECK(bits==0x800u&&state_hash()==before);
        }
    }
    CHECK(door_total>0u&&lock_total>0u);
    /* Explicit 60 Hz service timeline; output observations are independent.
     * This is test data, not a global fixed simulation timestep. */
    for(frame=1;frame<=960u;++frame){
        uint32_t next=frame*10000u/60u,elapsed=next-now;
        now=next;
        CHECK(dok_interactions_cadence(c2,elapsed,&c2)==DOK_STATUS_OK);
        if(frame==210u){
            uint16_t index=render.initial.glyph_index[blocked_glyph];
            CHECK(dok_collision_set_door_count(&collision,render.initial.objects[index-1u].reference,1)==DOK_STATUS_OK);
        }
        if(frame==330u){
            uint16_t index=render.initial.glyph_index[blocked_glyph];
            CHECK(dok_collision_set_door_count(&collision,render.initial.objects[index-1u].reference,0)==DOK_STATUS_OK);
        }
        CHECK(dok_events_dispatch(&scheduler,&dispatch)==DOK_STATUS_OK&&error==0u);
        if(frame==120u){
            for(i=0;i<256u;++i)if(interactions.doors[i].extent>0){
                CHECK(interactions.doors[i].state==1u&&interactions.doors[i].ready==1u);
                CHECK((collision.flags[i]&0x10u)!=0u);++opened;
            }
        }
        if(frame==270u)CHECK(interactions.doors[blocked_glyph].phase==59);
        if(hz!=0u&&(hz==60u||frame%2u==0u))CHECK(observation()==0);
    }
    for(i=0;i<render.initial.object_count;++i){
        const dok_level_object *o=&render.effective.objects[i];
        if(o->class_id==7u){
            const dok_level_field *mode=dok_level_field_at(o,0x5e);
            if(mode!=NULL&&mode->value==1){
                CHECK(interactions.doors[o->glyph].state==2u&&interactions.doors[o->glyph].ready==1u);
                CHECK(interactions.doors[o->glyph].phase==0&&collision.flags[o->glyph]==o->flags);++closed;
            }
        }
    }
    CHECK(opened==door_total&&closed>0u&&interactions.schedule_refusals==0u);
    /* Actual FCMap sensors and exits. These relocations are explicit fixtures. */
    {
        dok_motion_state m;uint32_t sensor=0,finish=0;
        CHECK(dok_motion_init(&m,0,-300,0,0,1,1)==DOK_STATUS_OK);
        for(i=0;i<4096u;++i){
            uint16_t index=render.initial.glyph_index[render.initial.maps[DOK_WORLD_FCMAP][i]];
            const dok_level_object *o=&render.effective.objects[index-1u];
            const dok_level_field *category=dok_level_field_at(o,0x66);
            if(!sensor&&o->class_id==13u){
                uint32_t revision;
                m.x=(int32_t)(i%64u)*480+240;m.z=(int32_t)(i/64u)*480+240;
                CHECK(dok_interactions_floor(&interactions,render.initial.hero,&m,0,c2)==DOK_STATUS_OK);
                revision=interactions.revision;
                CHECK(dok_interactions_floor(&interactions,render.initial.hero,&m,0,c2)==DOK_STATUS_OK);
                CHECK(revision==interactions.revision);sensor=1;
            }
            if(!finish&&o->class_id==12u&&category!=NULL&&category->value==1000){
                uint32_t old=outputs[DOK_INTERACTION_FINISH];
                m.x=(int32_t)(i%64u)*480+240;m.z=(int32_t)(i/64u)*480+240;
                CHECK(dok_interactions_floor(&interactions,render.initial.hero,&m,
                    (int16_t)(render.initial.category99_total-1),c2)==DOK_STATUS_OK);
                CHECK(outputs[DOK_INTERACTION_FINISH]==old);
                CHECK(dok_interactions_floor(&interactions,render.initial.hero,&m,render.initial.category99_total,c2)==DOK_STATUS_OK);
                CHECK(outputs[DOK_INTERACTION_FINISH]==old+1u&&(m.flags&DOK_MOTION_SLOW)!=0u);finish=1;
            }
        }
        CHECK(finish==1u);
        CHECK((render.effective.source_index==18u&&sensor==0u)||sensor==1u);
    }
    CHECK(conveyors()==0);
    *result=state_hash();*callbacks=callback_count;*door_count=door_total;return 0;
}
int main(int argc,char **argv)
{
    NdsAssetFile file;dok_asset_reader reader;uint32_t level,language;
    CHECK(argc==2);CHECK(nds_asset_file_open(&file,argv[1],&reader)==DOK_ASSET_OK);
    CHECK(dok_assets_open(&reader,&catalog,&session)==DOK_ASSET_OK);
    printf("{\"levels\":[");
    for(level=1;level<=3u;++level){
        dok_asset_view view;uint32_t base=0;
        dok_asset_id id=UINT32_C(2)<<30|(17u+level)<<18|(uint32_t)DOK_ASSET_LEVEL_CP866<<10;
        CHECK(dok_render_world_load(&session,level,200u+level,&render,&loader,assets,sizeof(assets))==DOK_STATUS_OK);
        baseline_world=render.initial;
        CHECK(dok_assets_load(&session,id,en_bytes,sizeof(en_bytes),&view)==DOK_ASSET_OK);
        CHECK(dok_level_parse(&view,&loader.world.level,&localized)==DOK_WORLD_OK);
        CHECK(dok_presentation_bind(&render.effective,&localized,&session,&loader.presentation,&en_presentation)==DOK_BALANCE_OK);
        for(language=1;language<=2u;++language){
            uint32_t mode;
            if(language==2u)render.presentation=en_presentation;
            for(mode=0;mode<3u;++mode){
                uint32_t result,callbacks,doors,hz=mode==0u?0u:mode==1u?30u:60u;
                CHECK(run(hz,&result,&callbacks,&doors)==0);
                if(language==1u&&mode==0u)base=result;else CHECK(base==result);
                printf("%s{\"level\":%lu,\"language\":%lu,\"observations_hz\":%lu,\"hash\":%lu,\"callbacks\":%lu,\"doors\":%lu,\"observations\":%lu,\"conveyor_cases\":%lu,\"conveyor_guards\":%lu}",
                    level==1u&&language==1u&&mode==0u?"":",",(unsigned long)level,(unsigned long)language,
                    (unsigned long)hz,(unsigned long)result,(unsigned long)callbacks,(unsigned long)doors,(unsigned long)observed,
                    (unsigned long)conveyor_cases,(unsigned long)conveyor_guards);
            }
        }
    }
    nds_asset_file_close(&file);
    printf("],\"checks\":%lu,\"failures\":%lu}\n",(unsigned long)checks,(unsigned long)failures);
    return failures!=0u;
}
