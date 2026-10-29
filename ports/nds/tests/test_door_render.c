#include "door_render_cases.h"
#include <stdio.h>
#include <string.h>
static dok_door_render_test_workspace unit;
static dok_level_definition definition;
static dok_initial_world world;
static dok_geometry_binding bindings[256];
static dok_interaction_phase_view phases;
static dok_render_scene scene;
static dok_nds_render_quad original[DOK_RENDER_QUADS];
static dok_collision_state collision;
static dok_interactions_state interactions;
static dok_event_bank bank;
static dok_event_scheduler scheduler;
static uint32_t clock_tick,callback_errors;
static uint32_t wrapper_checks;
#define CHECK(c) do{++wrapper_checks;if(!(c)){fprintf(stderr,"door_render_wrapper:%d\n",__LINE__);return 0;}}while(0)
static void fixture(uint32_t degenerate)
{
    uint32_t i;
    static const uint8_t glyphs[]={32,'F','D'};
    memset(&definition,0,sizeof(definition));memset(&world,0,sizeof(world));memset(bindings,0,sizeof(bindings));
    memset(&phases,0,sizeof(phases));
    definition.language=1;definition.object_count=3;
    definition.settings[0]=3;definition.settings[1]=1;definition.settings[2]=3;
    definition.settings[3]=480;definition.settings[4]=960;
    for(i=0;i<3u;++i) {
        dok_level_object *o=&definition.objects[i];o->glyph=glyphs[i];
        definition.glyph_index[glyphs[i]]=(uint16_t)(i+1u);
        o->field_count=1;o->fields[0].native_offset=0x54;o->fields[0].kind=DOK_LEVEL_WORD;o->fields[0].value=10;
    }
    definition.objects[2].class_id=7;definition.objects[2].width_parameter=(int16_t)(degenerate?480:5);
    world.definition=&definition;world.epoch=7;
    memset(world.maps[DOK_WORLD_TMAP],32,sizeof(world.maps[DOK_WORLD_TMAP]));
    memset(world.maps[DOK_WORLD_FCMAP],'F',sizeof(world.maps[DOK_WORLD_FCMAP]));
    for(i=3;i<6u;++i)world.maps[DOK_WORLD_TMAP][i]='D';
    for(i=1;i<4u;++i)bindings['F'].images[i]=(dok_geometry_image){(uint16_t)i,1,1,0};
    bindings['D'].images[0]=(dok_geometry_image){4,(uint16_t)(degenerate?1:96),(uint16_t)(degenerate?1:128),0x48};
    phases.world_epoch=7;phases.revision=1;phases.extent['D']=1;
    dok_render_scene_init(&scene);
}
static int wrapper(void)
{
    const dok_nds_render_frame *frame=NULL,*unchanged;
    dok_nds_render_camera camera={720,-300,-240,0,0};
    dok_door_render_counts counts,sentinel;
    uint32_t n,i,j,oldids[DOK_RENDER_QUADS],survivors;
    CHECK(dok_render_trig(0,&camera.sin_q15,&camera.cos_q15));
    fixture(0);CHECK(dok_render_scene_prepare(&scene,&world,bindings,&camera,0,&frame)==DOK_RENDER_SCENE_OK);
    n=scene.counts.published_quads;memcpy(original,scene.quads,n*sizeof(original[0]));
    CHECK(dok_render_scene_release(&scene,frame->generation)==DOK_RENDER_SCENE_OK);
    CHECK(dok_door_scene_prepare(&scene,&world,bindings,&camera,0,&phases,&counts,&frame)==DOK_RENDER_SCENE_OK);
    CHECK(counts.doors==3 && !counts.cropped && !counts.hidden && scene.counts.published_quads==n);
    CHECK(memcmp(original,scene.quads,n*sizeof(original[0]))==0);
    CHECK(dok_render_scene_release(&scene,frame->generation)==DOK_RENDER_SCENE_OK);
    phases.extent['D']=30;phases.phase['D']=13;
    CHECK(dok_door_scene_prepare(&scene,&world,bindings,&camera,0,&phases,&counts,&frame)==DOK_RENDER_SCENE_OK);
    CHECK(counts.doors==3 && counts.cropped==3 && !counts.hidden && scene.counts.published_quads==n);
    for(i=0;i<n;++i) {
        uint32_t cell=(original[i].source_surface_id-1u)/DOK_GEOMETRY_CELL_MAX;
        CHECK(scene.quads[i].source_surface_id==original[i].source_surface_id);
        if(cell/64u!=1u || (original[i].source_surface_id-1u)%DOK_GEOMETRY_CELL_MAX!=2u)
            CHECK(memcmp(&scene.quads[i],&original[i],sizeof(original[i]))==0);
    }
    CHECK(dok_render_scene_release(&scene,frame->generation)==DOK_RENDER_SCENE_OK);
    phases.phase['D']=29;
    CHECK(dok_door_scene_prepare(&scene,&world,bindings,&camera,0,&phases,&counts,&frame)==DOK_RENDER_SCENE_OK);
    CHECK(counts.hidden==3 && !counts.cropped && scene.counts.published_quads==n-3u);
    survivors=0;
    for(i=0;i<n;++i) {
        uint32_t cell=(original[i].source_surface_id-1u)/DOK_GEOMETRY_CELL_MAX;
        if(cell/64u==1u && (original[i].source_surface_id-1u)%DOK_GEOMETRY_CELL_MAX==2u)continue;
        oldids[survivors]=original[i].source_surface_id;
        CHECK(memcmp(&scene.quads[survivors++],&original[i],sizeof(original[i]))==0);
    }
    n=0;
    for(i=0;i<frame->cell_count;++i) {
        CHECK(frame->cells[i].quads==scene.quads+n && frame->cells[i].quad_count!=0u);
        for(j=0;j<frame->cells[i].quad_count;++j)CHECK(frame->cells[i].quads[j].source_surface_id==oldids[n++]);
    }
    CHECK(n==survivors);CHECK(dok_render_scene_release(&scene,frame->generation)==DOK_RENDER_SCENE_OK);
    memset(&sentinel,0xa5,sizeof(sentinel));counts=sentinel;unchanged=frame;phases.extent['D']=0;
    CHECK(dok_door_scene_prepare(&scene,&world,bindings,&camera,0,&phases,&counts,&frame)==DOK_RENDER_SCENE_GEOMETRY);
    CHECK(!scene.leased && frame==unchanged && memcmp(&counts,&sentinel,sizeof(counts))==0);
    phases.world_epoch=8;
    CHECK(dok_door_scene_prepare(&scene,&world,bindings,&camera,0,&phases,&counts,&frame)==DOK_RENDER_SCENE_ARGUMENT);
    CHECK(!scene.leased && frame==unchanged && memcmp(&counts,&sentinel,sizeof(counts))==0);
    fixture(1);phases.extent['D']=30;phases.phase['D']=1;
    CHECK(dok_door_scene_prepare(&scene,&world,bindings,&camera,0,&phases,&counts,&frame)==DOK_RENDER_SCENE_OK);
    CHECK(scene.counts.merged_quads>0 && !counts.cropped && !counts.hidden);
    CHECK(dok_render_scene_release(&scene,frame->generation)==DOK_RENDER_SCENE_OK);
    phases.phase['D']=29;unchanged=frame;counts=sentinel;
    CHECK(dok_door_scene_prepare(&scene,&world,bindings,&camera,0,&phases,&counts,&frame)==DOK_RENDER_SCENE_GEOMETRY);
    CHECK(!scene.leased && frame==unchanged && memcmp(&counts,&sentinel,sizeof(counts))==0);
    CHECK(dok_door_scene_prepare(&scene,&world,bindings,&camera,0,NULL,&counts,&frame)==DOK_RENDER_SCENE_OK);
    CHECK(dok_render_scene_release(&scene,frame->generation)==DOK_RENDER_SCENE_OK);
    return 1;
}
static dok_status read_clock(void *context,uint32_t *out)
{ (void)context;*out=clock_tick;return DOK_STATUS_OK; }
static void callback(void *context,dok_event_scheduler *events,dok_object_ref owner,uint16_t id,uint32_t parameter)
{
    (void)context;(void)events;(void)parameter;
    if(dok_interactions_callback(&interactions,owner,id)!=DOK_STATUS_OK)++callback_errors;
}
static int resampled_open_door(void)
{
    dok_event_config config={NULL,read_clock,NULL,callback,7};
    dok_interaction_services services={0};dok_event_dispatch_report dispatched;
    dok_nds_render_camera camera={720,-300,-240,0,0};dok_door_render_counts counts;
    const dok_nds_render_frame *frame;uint32_t i,bits;
    fixture(0);definition.object_count=4;definition.settings[7]=238;
    definition.objects[0].flags=16;definition.objects[1].flags=16;definition.objects[2].flags=64;
    definition.objects[3].class_id=1;definition.objects[3].glyph='H';definition.glyph_index['H']=4;
    world.object_count=4;memcpy(world.glyph_index,definition.glyph_index,sizeof(world.glyph_index));
    memset(world.maps[DOK_WORLD_MMAP],32,sizeof(world.maps[DOK_WORLD_MMAP]));
    for(i=0;i<4u;++i) {
        world.objects[i].reference=(dok_object_ref){7,i+1u,1};world.objects[i].definition_index=(uint16_t)i;
        world.objects[i].class_id=definition.objects[i].class_id;
    }
    world.hero=world.objects[3].reference;world.objects[3].pose_known=1;
    world.objects[3].x=720;world.objects[3].y=-300;world.objects[3].z=240;
    world.maps[DOK_WORLD_MMAP][1]='H';clock_tick=0;callback_errors=0;
    CHECK(dok_collision_bind(&collision,&world)==DOK_STATUS_OK);
    CHECK(dok_events_init(&scheduler,&bank,&config)==DOK_STATUS_OK);
    CHECK(dok_interactions_bind(&interactions,&world,&collision,&scheduler,&services)==DOK_STATUS_OK);
    CHECK(dok_interactions_register(&interactions)==DOK_STATUS_OK);
    bits=0x800;
    CHECK(dok_interactions_event(&interactions,world.objects[2].reference,world.hero,&bits,0,30)==DOK_STATUS_OK);
    for(i=416;i<=445u;++i) {clock_tick=i;CHECK(dok_events_dispatch(&scheduler,&dispatched)==DOK_STATUS_OK);}
    CHECK(!callback_errors && interactions.doors['D'].phase==29 && interactions.doors['D'].extent==30 &&
        interactions.doors['D'].state==1 && interactions.doors['D'].ready==1);
    /* This actual public event retains phase29 while replacing extent30 by8.
     * Render must accept the legal ready-open state, then resume closing. */
    bits=0x800;
    CHECK(dok_interactions_event(&interactions,world.objects[2].reference,world.hero,&bits,0,8)==DOK_STATUS_OK);
    CHECK(bits==0 && interactions.doors['D'].phase==29 && interactions.doors['D'].extent==8);
    CHECK(dok_interactions_phases(&interactions,&phases)==DOK_STATUS_OK);
    CHECK(dok_render_trig(0,&camera.sin_q15,&camera.cos_q15));
    CHECK(dok_door_scene_prepare(&scene,&world,bindings,&camera,0,&phases,&counts,&frame)==DOK_RENDER_SCENE_OK);
    CHECK(counts.hidden==3 && !counts.cropped);
    CHECK(dok_render_scene_release(&scene,frame->generation)==DOK_RENDER_SCENE_OK);
    clock_tick=861;CHECK(dok_events_dispatch(&scheduler,&dispatched)==DOK_STATUS_OK);
    CHECK(interactions.doors['D'].phase==28 && interactions.doors['D'].extent==8);
    CHECK(dok_interactions_phases(&interactions,&phases)==DOK_STATUS_OK);
    CHECK(dok_door_scene_prepare(&scene,&world,bindings,&camera,0,&phases,&counts,&frame)==DOK_RENDER_SCENE_OK);
    CHECK(counts.hidden==3 && !counts.cropped);
    CHECK(dok_render_scene_release(&scene,frame->generation)==DOK_RENDER_SCENE_OK);
    for(i=862;i<=883u;++i) {clock_tick=i;CHECK(dok_events_dispatch(&scheduler,&dispatched)==DOK_STATUS_OK);}
    CHECK(!callback_errors && interactions.doors['D'].phase==6);
    CHECK(dok_interactions_phases(&interactions,&phases)==DOK_STATUS_OK);
    CHECK(dok_door_scene_prepare(&scene,&world,bindings,&camera,0,&phases,&counts,&frame)==DOK_RENDER_SCENE_OK);
    CHECK(!counts.hidden && counts.cropped==3);
    CHECK(dok_render_scene_release(&scene,frame->generation)==DOK_RENDER_SCENE_OK);
    return 1;
}
int main(void)
{
    dok_door_render_test_report r;
    if(dok_door_render_run_cases(&unit,&r) || !wrapper() || !resampled_open_door())return 1;
    printf("{\"passed\":true,\"checks\":%lu,\"failures\":%lu,\"phase_cases\":%lu,\"sample_cases\":%lu,\"guard_cases\":%lu,\"wrapper_checks\":%lu,\"workspace_bytes\":%lu}\n",
        (unsigned long)r.checks,(unsigned long)r.failures,(unsigned long)r.phase_cases,
        (unsigned long)r.sample_cases,(unsigned long)r.guard_cases,(unsigned long)wrapper_checks,(unsigned long)sizeof(unit));
    return 0;
}
