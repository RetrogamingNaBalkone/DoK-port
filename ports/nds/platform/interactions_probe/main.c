/* N11 interactions diagnostic: explicit fixtures, six independent probes, no saves. */
#include <nds.h>
#include <fat.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dok/render_materials.h"
#include "dok/render_scene.h"
#include "dok/sha256.h"
#include "dok/controls.h"
#include "dok/motion.h"
#include "dok/camera.h"
#include "dok/time.h"
#include "dok/events.h"
#include "file_reader.h"
#include "render_backend.h"
#include "clock.h"
#include "input.h"
#include "interaction_cases.h"
#include "door_render_cases.h"
#include "interaction_router_cases.h"
#include "dok/door_render.h"
#include "dok/interactions_move.h"
#include "interactions_probe.h"

typedef struct InteractionLive {
    dok_render_world render;
    dok_input_buffer input;
    dok_control_latch controls,actions;
    dok_motion_state motion;
    dok_camera_history camera;
    dok_collision_state collision;
    dok_collision_context movement;
    dok_interactions_state interactions;
    dok_interaction_phase_view phases;
} InteractionLive;
typedef union InteractionWorld {
    InteractionLive live;
    dok_interaction_test_workspace unit;
    dok_interaction_router_workspace router;
} InteractionWorld;
typedef union InteractionScratch {
    dok_render_workspace render;
    dok_door_render_test_workspace unit;
} InteractionScratch;
typedef union InteractionIO {
    dok_asset_workspace catalog;
    unsigned char fixture[65536];
} InteractionIO;
_Static_assert(sizeof(InteractionWorld)<=DOK_NDS_WORLD_BYTES,"N11 world slab");
_Static_assert(sizeof(InteractionScratch)<=DOK_NDS_SCRATCH_BYTES,"N11 scratch slab");
_Static_assert(sizeof(InteractionIO)<=DOK_NDS_IO_BYTES,"N11 I/O slab");
extern unsigned char __end__, __eheap_end;
_Alignas(32) volatile NdsInteractionProbe g_nds_interaction_probe;
_Alignas(32) volatile NdsInteractionTraceRecord g_nds_interaction_trace[NDS_INTERACTION_TRACE_CAPACITY];
_Alignas(32) volatile NdsInteractionSnapshot g_nds_interaction_snapshot[NDS_INTERACTION_SNAPSHOT_CAPACITY];
static NdsInteractionProbe obs;
static volatile uint32_t vblanks;
static unsigned char *managed,*assets;
static InteractionWorld *world_storage;
static InteractionLive *world;
static InteractionScratch *scratch;
static InteractionIO *io;
static dok_render_scene *scene;
static dok_event_bank *bank;
static dok_world_clock clock_state;
static dok_event_scheduler scheduler;
static dok_nds_input sdk_input;
static dok_platform_services platform;
static dok_input_sample raw;
static dok_input_frame filtered_input;
static dok_camera_pose camera;
static dok_control_commands commands;
static dok_control_commands action_commands;
static dok_motion_report motion_report;
static dok_collision_report collision_report;
static dok_collision_report floor_report;
static const dok_world_object_seed *hero_seed;
static uint32_t camera_alpha,camera_fallback,camera_admitted;
static uint32_t latest_snapshot_ordinal,latest_snapshot_slot;
static uint32_t watch_started,watch_start,service_elapsed,first_service;
static uint32_t native_deadline,native_interval,input_flags,service_tick;
static uint32_t case_done,advance_requested,stage_callbacks,stage_sensor_entries;
static uint32_t blocked_auto,blocked_close,conveyor_callbacks,finish_baseline;
static uint32_t locked_baseline;
static uint32_t sweep_ready,sweep_paused,sweep_start,sweep_finish;
static uint32_t native_active,native_keep,native_glyph;
static uint32_t native_input[17],native_before[18],native_after[18];
static uint32_t native_deadline_before[3],native_deadline_after[3];
static NdsAssetFile package;
static dok_asset_session session;
static dok_nds_render_texture textures[DOK_NDS_RENDER_TEXTURE_CAPACITY];
static uint32_t fixture_count;
static int hud_bg,console_ready;
static PrintConsole bottom;

static void diagnostics(void);
static void snapshot(void);
static void trace(uint32_t action);
static int ui_boundary(void);
static int fixture_pose(int32_t x,int32_t z,int16_t heading);
static int stage_advance(void);
static int stage_progress(void);
static int advance_ready(void);

static uint32_t address(const void *p) { return (uint32_t)(uintptr_t)p; }
static void vblank_irq(void) { ++vblanks; }
static uint32_t le32(const unsigned char *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }

static void clock_probe(void)
{
    uint64_t counter=platform.read_counter?platform.read_counter(platform.context):0;
    obs.raw_counter_low=(uint32_t)counter;obs.raw_counter_high=(uint32_t)(counter>>32);
    obs.clock_last_counter_low=(uint32_t)clock_state._last_counter;
    obs.clock_last_counter_high=(uint32_t)(clock_state._last_counter>>32);
    obs.rate_numerator_low=(uint32_t)clock_state._numerator;
    obs.rate_numerator_high=(uint32_t)(clock_state._numerator>>32);
    obs.rate_denominator_low=(uint32_t)clock_state._denominator;
    obs.rate_denominator_high=(uint32_t)(clock_state._denominator>>32);
    obs.fraction_low=(uint32_t)clock_state._fraction;
    obs.fraction_high=(uint32_t)(clock_state._fraction>>32);
    obs.native_now=clock_state._ticks;obs.paused=clock_state._paused;
}
static void publish(void)
{
    clock_probe();
    obs.advance_ready=world && obs.status==NDS_INTERACTION_LIVE ? (uint32_t)advance_ready():0;
    if(world && world->interactions.revision) {
        const dok_interaction_door *d=&world->interactions.doors[obs.door_glyph];
        obs.interaction_revision=world->interactions.revision;
        obs.phase_revision=world->phases.revision;
        obs.door_phase=(uint32_t)(int32_t)d->phase;obs.door_extent=(uint32_t)(int32_t)d->extent;
        obs.door_state=d->state;obs.door_ready=d->ready;
    }
    obs.magic=NDS_INTERACTION_PROBE_MAGIC;obs.abi_version=NDS_INTERACTION_ABI_VERSION;
    obs.size_bytes=sizeof(obs);obs.sequence=g_nds_interaction_probe.sequence+1u;
    g_nds_interaction_probe.sequence=obs.sequence;
    DC_FlushRange((const void *)&g_nds_interaction_probe.sequence,32);
    obs.vblank_count=vblanks;
    memcpy((void *)&g_nds_interaction_probe,&obs,sizeof(obs));
    DC_FlushRange((const void *)&g_nds_interaction_probe,sizeof(obs));
    ++g_nds_interaction_probe.sequence;
    DC_FlushRange((const void *)&g_nds_interaction_probe.sequence,32);
}
static void trace(uint32_t action)
{
    NdsInteractionTraceRecord row={0};
    const dok_motion_state *m=&world->motion;
    uint32_t index=obs.trace_total%NDS_INTERACTION_TRACE_CAPACITY;
    clock_probe();
    row.magic=NDS_INTERACTION_TRACE_MAGIC;row.abi_version=NDS_INTERACTION_ABI_VERSION;
    row.size_bytes=sizeof(row);row.sequence=g_nds_interaction_trace[index].sequence+1u;
    row.ordinal=++obs.trace_total;row.action=action;row.frame_id=obs.frame_id;
    row.vblank_count=vblanks;row.status=obs.status;
    row.flags=m->flags;row.x=(uint32_t)m->x;row.y=(uint32_t)m->y;row.z=(uint32_t)m->z;
    row.heading=(uint32_t)(int32_t)m->heading;
    row.angle_parameter=(uint32_t)(int32_t)m->angle_parameter;row.speed=(uint32_t)m->speed;
    row.distance=(uint32_t)m->distance;row.turn_step=(uint32_t)(int32_t)m->turn_step;
    row.linear_step=(uint32_t)(int32_t)m->linear_step;row.last_turn=(uint32_t)(int32_t)m->last_turn;
    row.movement_accumulator=(uint32_t)m->movement_accumulator;
    row.turn_accumulator=(uint32_t)m->turn_accumulator;row.accel_sign=(uint32_t)(int32_t)m->accel_sign;
    row.last_flags=m->last_flags;row.moved=motion_report.moved;row.substeps=motion_report.substeps;
    row.service_elapsed=service_elapsed;row.first_service=first_service;
    row.native_commands=commands.native_commands;row.physical_held=commands.physical_held;
    row.physical_pressed=commands.physical_pressed;row.physical_released=commands.physical_released;
    row.use_triggered=commands.use_triggered;row.next_weapon_pressed=commands.next_weapon_pressed;
    row.camera_x=(uint32_t)camera.x;row.camera_y=(uint32_t)camera.y;row.camera_z=(uint32_t)camera.z;
    row.camera_heading_q16=camera.heading_q16;
    row.raw_held=raw.frame.held;row.raw_pressed=raw.frame.pressed;row.raw_released=raw.frame.released;
    row.raw_touch=raw.frame.touch_flags;row.raw_touch_x=(uint32_t)(int32_t)raw.frame.touch_x;
    row.raw_touch_y=(uint32_t)(int32_t)raw.frame.touch_y;
    row.raw_sequence_low=(uint32_t)raw.frame.sequence;row.raw_sequence_high=(uint32_t)(raw.frame.sequence>>32);
    row.raw_counter_low=obs.raw_counter_low;row.raw_counter_high=obs.raw_counter_high;
    row.clock_last_counter_low=obs.clock_last_counter_low;row.clock_last_counter_high=obs.clock_last_counter_high;
    row.fraction_low=obs.fraction_low;row.fraction_high=obs.fraction_high;
    row.native_now=obs.native_now;row.paused=obs.paused;
    row.callback_total=obs.callback_total;row.render_reads=obs.render_reads;
    row.input_total=obs.input_total;row.pause_count=obs.pause_count;row.resume_count=obs.resume_count;
    row.native_deadline=native_deadline;row.native_interval=native_interval;
    row.input_flags=input_flags;
    row.service_tick=service_tick;row.watch_start=watch_start;row.watch_started=watch_started;
    row.sample_counter_low=(uint32_t)raw.counter;row.sample_counter_high=(uint32_t)(raw.counter>>32);
    /* Input records observe the admitted sample; UI boundary below may consume
     * it before gameplay. Callback records instead expose service commands. */
    if(action==NDS_INTERACTION_ACTION_INPUT) {
        row.physical_held=filtered_input.held;row.physical_pressed=filtered_input.pressed;
        row.physical_released=filtered_input.released;
    }
    if(action==NDS_INTERACTION_ACTION_USE) {
        row.physical_held=action_commands.physical_held;row.physical_pressed=action_commands.physical_pressed;
        row.physical_released=action_commands.physical_released;row.native_commands=action_commands.native_commands;
        row.use_triggered=action_commands.use_triggered;row.next_weapon_pressed=action_commands.next_weapon_pressed;
    }
    row.physical_touch=filtered_input.touch_flags;
    row.level_id=obs.level_id;row.world_epoch=obs.world_epoch;row.level_callback_total=obs.level_callback_total;
    row.hero_id=obs.hero_id;row.hero_generation=obs.hero_generation;row.hero_glyph=obs.hero_glyph;
    row.upper_y=hero_seed?(uint32_t)(int32_t)hero_seed->upper_y:0u;
    row.lower_y=hero_seed?(uint32_t)(int32_t)hero_seed->lower_y:0u;
    row.collision_requested=collision_report.requested;row.collision_committed=collision_report.committed;
    row.collision_adjusted=collision_report.adjusted;row.collision_rejected=collision_report.rejected;
    row.collision_reason_mask=collision_report.reason_mask;
    row.camera_alpha_q16=camera_alpha;row.camera_previous_tick=world->camera.previous.tick;
    row.camera_current_tick=world->camera.current.tick;row.camera_fallback=camera_fallback;
    row.camera_admitted=camera_admitted;
    if(action==NDS_INTERACTION_ACTION_LEVEL_BEGIN||action==NDS_INTERACTION_ACTION_CALLBACK){
        row.snapshot_ordinal=latest_snapshot_ordinal;row.snapshot_slot=latest_snapshot_slot;
    }
    row.case_id=obs.case_id;row.stage_id=obs.stage_id;row.stage_sequence=obs.stage_sequence;
    row.stage_started=obs.stage_started;row.c2=obs.c2;row.sweep_ticks=obs.sweep_ticks;
    row.sweep_begin=obs.sweep_begin;row.sweep_end=obs.sweep_end;row.sweep_total=obs.sweep_total;
    row.keys=obs.keys;row.score=obs.score;row.door_glyph=obs.door_glyph;
    row.interaction_revision=world->interactions.revision;row.phase_revision=world->phases.revision;
    row.locked_total=obs.locked_total;row.finish_total=obs.finish_total;
    row.door_callbacks=obs.door_callbacks;row.sensor_entries=obs.sensor_entries;
    row.fixture_occupancy=obs.fixture_occupancy;row.rendered_doors=obs.rendered_doors;
    row.rendered_cropped=obs.rendered_cropped;row.rendered_hidden=obs.rendered_hidden;
    row.advance_ready=(uint32_t)advance_ready();
    if(action==NDS_INTERACTION_ACTION_FIXTURE || action==NDS_INTERACTION_ACTION_FLOOR ||
       action==NDS_INTERACTION_ACTION_OPEN || action==NDS_INTERACTION_ACTION_CLOSE ||
       action==NDS_INTERACTION_ACTION_AUTO || action==NDS_INTERACTION_ACTION_USE) {
        row.snapshot_ordinal=latest_snapshot_ordinal;row.snapshot_slot=latest_snapshot_slot;
    }
    row.native_valid=native_active && (action==NDS_INTERACTION_ACTION_USE || action==NDS_INTERACTION_ACTION_FLOOR ||
        action==NDS_INTERACTION_ACTION_OPEN || action==NDS_INTERACTION_ACTION_CLOSE || action==NDS_INTERACTION_ACTION_AUTO);
    row.native_keep=native_keep;row.native_target_glyph=native_glyph;
    if(row.native_valid) {
        memcpy(row.native_input,native_input,sizeof(native_input));
        memcpy(row.native_before,native_before,sizeof(native_before));
        memcpy(row.native_after,native_after,sizeof(native_after));
        memcpy(row.queue_before_deadline,native_deadline_before,sizeof(native_deadline_before));
        memcpy(row.queue_after_deadline,native_deadline_after,sizeof(native_deadline_after));
    }
    g_nds_interaction_trace[index].sequence=row.sequence;
    DC_FlushRange((const void *)&g_nds_interaction_trace[index].sequence,32);
    memcpy((void *)&g_nds_interaction_trace[index],&row,sizeof(row));
    DC_FlushRange((const void *)&g_nds_interaction_trace[index],sizeof(row));
    ++g_nds_interaction_trace[index].sequence;
    DC_FlushRange((const void *)&g_nds_interaction_trace[index].sequence,32);
}

static void snapshot(void)
{
    NdsInteractionSnapshotHeader h={0};
    uint32_t index=obs.snapshot_total%NDS_INTERACTION_SNAPSHOT_CAPACITY,i;
    h.magic=NDS_INTERACTION_SNAPSHOT_MAGIC;h.abi_version=NDS_INTERACTION_ABI_VERSION;
    h.size_bytes=sizeof(NdsInteractionSnapshot);
    h.sequence=g_nds_interaction_snapshot[index].header.sequence+1u;
    h.ordinal=++obs.snapshot_total;h.level_id=obs.level_id;h.world_epoch=obs.world_epoch;
    h.callback_total=obs.callback_total;h.trace_ordinal=obs.trace_total+1u;
    h.width=(uint16_t)world->render.initial.definition->settings[DOK_LEVEL_WIDTH];
    h.length=(uint16_t)world->render.initial.definition->settings[DOK_LEVEL_LENGTH];
    h.hero_glyph=obs.hero_glyph;h.hero_id=obs.hero_id;h.hero_generation=obs.hero_generation;
    h.case_id=obs.case_id;h.stage_sequence=obs.stage_sequence;
    g_nds_interaction_snapshot[index].header.sequence=h.sequence;
    DC_FlushRange((const void *)&g_nds_interaction_snapshot[index].header.sequence,32);
    memcpy((void *)&g_nds_interaction_snapshot[index].maps,&world->render.initial.maps,sizeof(world->render.initial.maps));
    memcpy((void *)&g_nds_interaction_snapshot[index].flags,&world->collision.flags,sizeof(world->collision.flags));
    memcpy((void *)&g_nds_interaction_snapshot[index].door_count,&world->collision.door_count,sizeof(world->collision.door_count));
    memcpy((void *)&g_nds_interaction_snapshot[index].known,&world->collision.known,sizeof(world->collision.known));
    for(i=0;i<256u;++i) {
        const dok_interaction_door *d=&world->interactions.doors[i];
        g_nds_interaction_snapshot[index].phase[i]=d->phase;g_nds_interaction_snapshot[index].extent[i]=d->extent;
        g_nds_interaction_snapshot[index].state[i]=d->state;g_nds_interaction_snapshot[index].ready[i]=d->ready;
        g_nds_interaction_snapshot[index].marker[i]=d->marker;
    }
    memcpy((void *)&g_nds_interaction_snapshot[index].header,&h,sizeof(h));
    DC_FlushRange((const void *)&g_nds_interaction_snapshot[index],sizeof(NdsInteractionSnapshot));
    ++g_nds_interaction_snapshot[index].header.sequence;
    DC_FlushRange((const void *)&g_nds_interaction_snapshot[index].header.sequence,32);
    latest_snapshot_ordinal=h.ordinal;latest_snapshot_slot=index;
}
static int memory_setup(void)
{
    void *allocation;uint32_t cursor=0;
    obs.region_base=DOK_NDS_MAIN_BASE;obs.region_limit=DOK_NDS_ARM9_LIMIT;
    obs.static_end=address(&__end__);obs.heap_start=address(getHeapStart());
    obs.heap_limit_before=address(getHeapLimit());obs.is_dsi=isDSiMode();
    obs.reserve_bytes=DOK_NDS_RESERVE_BYTES;obs.sdk_heap_min=DOK_NDS_SDK_HEAP_MIN;
    if(obs.is_dsi || address(&__eheap_end)!=DOK_NDS_ARM9_LIMIT ||
       obs.heap_limit_before<DOK_NDS_ARM9_LIMIT ||
       obs.static_end>DOK_NDS_MAIN_BASE+DOK_NDS_STATIC_CEILING) return 0;
    if(obs.heap_limit_before>DOK_NDS_ARM9_LIMIT &&
       reduceHeapSize(obs.heap_limit_before-DOK_NDS_ARM9_LIMIT)!=0) return 0;
    obs.heap_limit_policy=address(getHeapLimit());
    if(reduceHeapSize(DOK_NDS_RESERVE_BYTES)!=0) return 0;
    obs.malloc_request_bytes=DOK_NDS_SLAB_BYTES+31u;
    allocation=malloc(obs.malloc_request_bytes);if(!allocation) return 0;
    managed=(unsigned char *)(((uintptr_t)allocation+31u)&~(uintptr_t)31u);
    obs.managed_base=address(managed);obs.managed_bytes=DOK_NDS_SLAB_BYTES;
    world_storage=(InteractionWorld *)(void *)(managed+cursor);world=&world_storage->live;cursor+=DOK_NDS_WORLD_BYTES;
    bank=(dok_event_bank *)(void *)(managed+cursor);cursor+=DOK_NDS_EVENTS_BYTES;
    scratch=(InteractionScratch *)(void *)(managed+cursor);cursor+=DOK_NDS_SCRATCH_BYTES;
    scene=(dok_render_scene *)(void *)(managed+cursor);cursor+=DOK_NDS_SCENE_BYTES;
    assets=managed+cursor;cursor+=DOK_NDS_ASSETS_BYTES;cursor+=DOK_NDS_AUDIO_BYTES;
    io=(InteractionIO *)(void *)(managed+cursor);cursor+=DOK_NDS_IO_BYTES;
    if(cursor!=DOK_NDS_SLAB_BYTES) return 0;
    memset(world_storage,0,sizeof(*world_storage));
    obs.world_used=sizeof(*world);obs.world_high_water=sizeof(*world_storage);
    obs.scratch_used=obs.scratch_high_water=sizeof(*scratch);
    obs.scene_used=obs.scene_high_water=sizeof(*scene);
    obs.events_bank_bytes=sizeof(*bank);return 1;
}
static int memory_check(void)
{
    uint32_t heap=address(getHeapEnd());obs.heap_end=heap;obs.heap_limit_final=address(getHeapLimit());
    if(heap<obs.static_end+DOK_NDS_SLAB_BYTES || heap>obs.heap_limit_final) return 0;
    obs.static_startup_bytes=heap-DOK_NDS_MAIN_BASE-DOK_NDS_SLAB_BYTES;
    obs.sdk_heap_available=obs.heap_limit_final-heap;
    return obs.heap_limit_final==DOK_NDS_ARM9_LIMIT-DOK_NDS_RESERVE_BYTES &&
        obs.static_startup_bytes<=DOK_NDS_STATIC_CEILING && obs.sdk_heap_available>=DOK_NDS_SDK_HEAP_MIN;
}
static int fixture_load(void)
{
    FILE *file=fopen("fat:/dok/scene.dat","rb");
    uint32_t bytes;unsigned char digest[32];dok_sha256 hash;
    if(!file) return 0;
    bytes=(uint32_t)fread(io->fixture,1,sizeof(io->fixture),file);
    if(fgetc(file)!=EOF || ferror(file)) {fclose(file);return 0;}fclose(file);
    if(bytes<64u || memcmp(io->fixture,"N07SCN1\0",8) || le32(io->fixture+8)!=1u ||
       le32(io->fixture+16)!=64u || le32(io->fixture+20)!=10240u || le32(io->fixture+28)) return 0;
    fixture_count=le32(io->fixture+12);
    if(!fixture_count || fixture_count>64u || bytes!=64u+fixture_count*64u+30720u ||
       le32(io->fixture+24)!=bytes) return 0;
    dok_sha256_begin(&hash);dok_sha256_update(&hash,io->fixture+64,bytes-64);
    dok_sha256_end(&hash,digest);return memcmp(digest,io->fixture+32,32)==0;
}
static const unsigned char *level_block(void)
{ return io->fixture+64u+fixture_count*64u+(obs.level_id-1u)*10240u; }
static int package_open(void)
{
    dok_asset_reader reader;
    return fatInitDefault()&&nds_asset_file_open(&package,"fat:/dok/assets.ndp",&reader)==DOK_ASSET_OK&&
        dok_assets_open(&reader,&io->catalog,&session)==DOK_ASSET_OK&&fixture_load();
}
static int load_world(uint32_t level)
{
    dok_nds_render_stats stats={0};uint32_t i;
    obs.status=NDS_INTERACTION_LOADING;obs.level_id=level;obs.world_epoch=210u+obs.case_id;
    obs.frame_id=0;obs.level_callback_total=0;publish();
    memset(world,0,sizeof(*world));hero_seed=NULL;
    if(dok_render_world_load(&session,level,obs.world_epoch,&world->render,&scratch->render,assets,
        DOK_NDS_ASSETS_BYTES)!=DOK_STATUS_OK||
       dok_collision_bind(&world->collision,&world->render.initial)!=DOK_STATUS_OK)return 0;
    for(i=0;i<world->render.material_count;++i) {
        const dok_render_material *m=&world->render.materials[i];
        textures[i]=(dok_nds_render_texture){m->entry.id,m->pixels,m->entry.bytes,
            (const uint16_t *)(const void *)level_block(),m->entry.storage_width,
            m->entry.storage_height,m->entry.width,m->entry.height,m->color0_transparent};
    }
    if(dok_nds_render_preload(textures,world->render.material_count,&stats)!=DOK_NDS_RENDER_OK)return 0;
    DC_FlushRange(level_block(),10240);
    dmaCopy(level_block(),BG_PALETTE,512);dmaFillHalfWords(0,bgGetGfxPtr(hud_bg),65536);
    dmaCopy(level_block()+512,(unsigned char *)bgGetGfxPtr(hud_bg)+154u*256u,9728);
    obs.assets_used=world->render.asset_bytes;
    if(obs.assets_high_water<obs.assets_used)obs.assets_high_water=obs.assets_used;
    dok_render_scene_init(scene);return memory_check();
}
static int trig(void *context,uint32_t heading,int32_t *sine,int32_t *cosine)
{ (void)context;return dok_render_trig(heading,sine,cosine); }
static const dok_world_object_seed *seed_for_glyph(uint32_t glyph)
{
    uint32_t index=world->render.initial.glyph_index[glyph];
    return index && index<=world->render.initial.object_count ? &world->render.initial.objects[index-1u]:NULL;
}
static const dok_level_object *definition_for_glyph(uint32_t glyph)
{
    const dok_world_object_seed *seed=seed_for_glyph(glyph);
    return seed ? &world->render.effective.objects[seed->definition_index]:NULL;
}
static int32_t object_field(const dok_level_object *o,uint16_t offset)
{
    const dok_level_field *f=o ? dok_level_field_at(o,offset):NULL;
    return f ? f->value:0;
}
static void native_state(uint32_t glyph,uint32_t bits,uint32_t out[18],uint32_t deadlines[3])
{
    const dok_level_object *o=definition_for_glyph(glyph);
    const dok_world_object_seed *seed=seed_for_glyph(glyph);
    const dok_event_owner *owner=NULL;uint32_t i;
    const dok_interaction_door *d=&world->interactions.doors[glyph];
    memset(out,0,18u*sizeof(*out));memset(deadlines,0,3u*sizeof(*deadlines));
    out[0]=world->collision.flags[glyph];out[1]=(uint32_t)(int32_t)d->phase;
    out[2]=d->state;out[3]=d->ready;out[4]=(uint32_t)(int32_t)(int16_t)world->collision.door_count[glyph];
    out[5]=(uint32_t)(int32_t)d->extent;out[6]=(uint32_t)(int32_t)(int16_t)object_field(o,0x52);
    out[7]=bits;out[8]=d->marker;
    if(seed && dok_events_inspect_owner(&scheduler,seed->reference,&owner)==DOK_STATUS_OK) {
        out[9]=owner->count;
        for(i=0;i<owner->count && i<3u;++i) {
            out[10u+i*2u]=(uint32_t)owner->events[i].callback-DOK_INTERACTION_OPEN+1u;
            out[11u+i*2u]=owner->events[i].interval;deadlines[i]=owner->events[i].deadline;
        }
    }
}
static void native_begin(uint32_t op,uint32_t glyph,uint32_t bits,uint32_t source_class,uint16_t keys)
{
    const dok_level_object *o=definition_for_glyph(glyph);
    const dok_interaction_door *d=&world->interactions.doors[glyph];
    native_active=1;native_keep=1;native_glyph=glyph;
    native_input[0]=op;native_input[1]=world->collision.flags[glyph];native_input[2]=(uint32_t)(int32_t)d->phase;
    native_input[3]=d->state;native_input[4]=d->ready;
    native_input[5]=(uint32_t)(int32_t)(int16_t)world->collision.door_count[glyph];
    native_input[6]=(uint32_t)(int32_t)d->extent;native_input[7]=(uint16_t)object_field(o,0x5c);
    native_input[8]=(uint32_t)(int32_t)(int16_t)object_field(o,0x5e);
    native_input[9]=(uint32_t)object_field(o,0x66);native_input[10]=(uint32_t)(int32_t)(int16_t)object_field(o,0x52);
    native_input[11]=bits;native_input[12]=keys;native_input[13]=source_class;native_input[14]=obs.c2;
    native_input[15]=d->marker;native_input[16]=o ? o->flags:0;
    native_state(glyph,bits,native_before,native_deadline_before);
}
static void native_end(uint32_t bits,uint32_t action)
{
    native_state(native_glyph,bits,native_after,native_deadline_after);
    native_keep=scheduler._in_callback?scheduler._keep:1u;
    snapshot();trace(action);native_active=0;
}
static void interaction_sink(void *context,const dok_interaction_output *out)
{
    (void)context;
    if(out->kind==DOK_INTERACTION_LOCKED)++obs.locked_total;
    if(out->kind==DOK_INTERACTION_FINISH)++obs.finish_total;
}
static dok_status interaction_move(void *context,dok_object_ref actor,dok_motion_state *motion,int32_t x,int32_t z)
{
    dok_status status;
    (void)context;
    status=dok_collision_displace(&world->collision,actor,motion,x,z,&floor_report);
    if(status==DOK_STATUS_OK && (floor_report.adjusted || floor_report.rejected))dok_camera_reset(&world->camera);
    return status;
}
static uint32_t floor_glyph(void)
{
    uint32_t x=(uint32_t)world->motion.x/480u,z=(uint32_t)world->motion.z/480u;
    return world->render.initial.maps[DOK_WORLD_FCMAP][z*64u+x];
}
static void hero_callback(void *context,dok_event_scheduler *events,dok_object_ref ref,
                          uint16_t id,uint32_t parameter)
{
    uint32_t now,after;const dok_event_owner *owner;dok_status updated;
    const dok_motion_services services={NULL,trig,&world->movement,dok_collision_move};
    (void)context;(void)id;(void)parameter;
    if(dok_events_inspect_owner(events,ref,&owner)!=DOK_STATUS_OK||!owner->count||
       dok_world_clock_read(&clock_state,&now)!=DOK_STATUS_OK||
       dok_controls_take(&world->controls,&commands)!=DOK_STATUS_OK){obs.error_bits|=16;return;}
    native_deadline=owner->events[0].deadline;native_interval=owner->events[0].interval;
    service_tick=now;first_service=!watch_started;service_elapsed=watch_started?now-watch_start:0u;
    commands.native_commands&=~UINT32_C(0x800);commands.use_triggered=0;
    world->motion.flags=(world->motion.flags&~UINT32_C(0x00182a3c))|commands.native_commands;
    input_flags=world->motion.flags;
    memset(&floor_report,0,sizeof(floor_report));memset(&collision_report,0,sizeof(collision_report));
    trace(NDS_INTERACTION_ACTION_HERO_BEGIN);
    {
        uint32_t glyph=floor_glyph();const dok_level_object *o=definition_for_glyph(glyph);
        const dok_world_object_seed *floor_seed=seed_for_glyph(glyph);
        uint32_t sensor=o && o->class_id==13u && floor_seed &&
            !dok_object_ref_equal(world->interactions.previous_floor,floor_seed->reference);
        int32_t oldx=world->motion.x,oldz=world->motion.z;
        if(sensor) {
            native_begin(5u,(uint8_t)object_field(o,0x66),(uint32_t)object_field(o,0x67),13u,0);
            ++obs.sensor_entries;
        }
        if(dok_interactions_floor(&world->interactions,ref,&world->motion,(int16_t)obs.score,obs.c2)!=DOK_STATUS_OK){obs.error_bits|=16;return;}
        if(world->motion.x!=oldx || world->motion.z!=oldz)++conveyor_callbacks;
        collision_report=floor_report;
        obs.collision_requested+=floor_report.requested;obs.collision_committed+=floor_report.committed;
        obs.collision_adjusted+=floor_report.adjusted;obs.collision_rejected+=floor_report.rejected;
        obs.collision_reason_mask|=floor_report.reason_mask;
        if(sensor)native_end(native_input[11],NDS_INTERACTION_ACTION_FLOOR);
        else {snapshot();trace(NDS_INTERACTION_ACTION_FLOOR);}
    }
    if(dok_collision_begin(&world->collision,ref,&world->motion,service_elapsed,&world->movement)!=DOK_STATUS_OK){
        obs.error_bits|=16;return;
    }
    updated=dok_motion_update(&world->motion,service_elapsed,&services,&motion_report);
    if(dok_collision_end(&world->movement,&collision_report)!=DOK_STATUS_OK||updated!=DOK_STATUS_OK||
       (collision_report.reason_mask&DOK_COLLISION_BAD_REQUEST)!=0u){obs.error_bits|=16;return;}
    if(collision_report.adjusted||collision_report.rejected)dok_camera_reset(&world->camera);
    if(dok_world_clock_read(&clock_state,&after)!=DOK_STATUS_OK||
       dok_camera_commit(&world->camera,&world->motion,after)!=DOK_STATUS_OK){obs.error_bits|=16;return;}
    watch_start=after;watch_started=1u;++obs.callback_total;++obs.level_callback_total;
    obs.collision_requested+=collision_report.requested;obs.collision_committed+=collision_report.committed;
    obs.collision_adjusted+=collision_report.adjusted;obs.collision_rejected+=collision_report.rejected;
    obs.collision_reason_mask|=collision_report.reason_mask;
    snapshot();trace(NDS_INTERACTION_ACTION_CALLBACK);
}
static void event_callback(void *context,dok_event_scheduler *events,dok_object_ref ref,
    uint16_t id,uint32_t parameter)
{
    const dok_world_object_seed *seed;uint32_t glyph,action;
    if(id==1u && dok_object_ref_equal(ref,world->render.initial.hero)) {
        hero_callback(context,events,ref,id,parameter);return;
    }
    seed=NULL;
    for(glyph=0;glyph<world->render.initial.object_count;++glyph)
        if(dok_object_ref_equal(world->render.initial.objects[glyph].reference,ref))seed=&world->render.initial.objects[glyph];
    if(!seed){obs.error_bits|=16;return;}
    glyph=world->render.effective.objects[seed->definition_index].glyph;
    action=id==DOK_INTERACTION_OPEN?NDS_INTERACTION_ACTION_OPEN:
        id==DOK_INTERACTION_CLOSE?NDS_INTERACTION_ACTION_CLOSE:NDS_INTERACTION_ACTION_AUTO;
    native_begin((uint32_t)id-DOK_INTERACTION_OPEN+1u,glyph,0,1u,0);
    if(glyph==obs.door_glyph && (int16_t)world->collision.door_count[glyph]>0) {
        if(id==DOK_INTERACTION_AUTO)++blocked_auto;
        if(id==DOK_INTERACTION_CLOSE)++blocked_close;
    }
    if(dok_interactions_callback(&world->interactions,ref,id)!=DOK_STATUS_OK){obs.error_bits|=16;return;}
    ++obs.door_callbacks;native_end(0,action);
}
static int ui_boundary(void)
{ return dok_input_ui_boundary(&world->input)==DOK_STATUS_OK &&
         dok_controls_ui_boundary(&world->controls)==DOK_STATUS_OK &&
         dok_controls_ui_boundary(&world->actions)==DOK_STATUS_OK; }
static int expects_advance(void)
{
    switch(obs.stage_id) {
    case NDS_INTERACTION_AUTO_BLOCKED:case NDS_INTERACTION_CLOSE_BLOCKED:
    case NDS_INTERACTION_CLOSED:case NDS_INTERACTION_SENSOR_FIRST:
    case NDS_INTERACTION_SENSOR_SAME:case NDS_INTERACTION_SENSOR_OUT:
    case NDS_INTERACTION_SENSOR_REENTER:case NDS_INTERACTION_CONVEYOR:
    case NDS_INTERACTION_FINISH_WAIT:case NDS_INTERACTION_FINISH_SENT:return 1;
    case NDS_INTERACTION_OPEN_OCCUPIED:return obs.case_id==NDS_INTERACTION_CASE_MANUAL_H;
    default:return 0;
    }
}
static int advance_ready(void)
{
    if(!expects_advance())return 0;
    switch(obs.stage_id) {
    case NDS_INTERACTION_SENSOR_FIRST:case NDS_INTERACTION_SENSOR_SAME:
    case NDS_INTERACTION_SENSOR_OUT:case NDS_INTERACTION_SENSOR_REENTER:
    case NDS_INTERACTION_FINISH_WAIT:return obs.callback_total>stage_callbacks;
    case NDS_INTERACTION_CONVEYOR:return conveyor_callbacks!=0u;
    default:return 1;
    }
}
static int poll_input(void)
{
    dok_input_sample filtered;uint32_t available;
    if(dok_nds_input_poll(&sdk_input,&raw)!=DOK_STATUS_OK ||
       dok_input_capture(&world->input,&raw.frame,raw.counter)!=DOK_STATUS_OK) return 0;
    /* Read-only telemetry of the exact captured sample, before any drain or
     * UI boundary. Core owns the queue and remains its only writer. */
    filtered_input=world->input._samples[(world->input._head+world->input._count-1u)%DOK_INPUT_SAMPLE_LIMIT].frame;
    ++obs.input_total;obs.buttons_seen|=raw.frame.held|raw.frame.pressed;
    obs.touch_seen|=raw.frame.touch_flags;trace(NDS_INTERACTION_ACTION_INPUT);
    if(obs.status==NDS_INTERACTION_LOADING)return ui_boundary();
    if(raw.frame.pressed&DOK_BUTTON_START) {
        if(obs.status==NDS_INTERACTION_PAUSED) {
            if(!ui_boundary() || dok_world_clock_resume(&clock_state)!=DOK_STATUS_OK) return 0;
            obs.status=NDS_INTERACTION_LIVE;++obs.resume_count;trace(NDS_INTERACTION_ACTION_RESUME);
        } else {
            if(dok_world_clock_pause(&clock_state)!=DOK_STATUS_OK || !ui_boundary()) return 0;
            obs.status=NDS_INTERACTION_PAUSED;++obs.pause_count;trace(NDS_INTERACTION_ACTION_PAUSE);
        }
        return 1;
    }
    if(obs.status==NDS_INTERACTION_PAUSED) return ui_boundary();
    if((raw.frame.pressed&DOK_BUTTON_A) && expects_advance()) {
        if(advance_ready())advance_requested=1;
        return ui_boundary();
    }
    do {
        if(dok_input_pop_through(&world->input,raw.counter,&filtered,&available)!=DOK_STATUS_OK) return 0;
        if(available && (dok_controls_feed(&world->controls,&filtered.frame)!=DOK_STATUS_OK ||
            dok_controls_feed(&world->actions,&filtered.frame)!=DOK_STATUS_OK)) return 0;
    } while(available);
    return 1;
}
static int action_service(void)
{
    uint32_t bits,glyph,index;
    int32_t sine,cosine,x,z;
    const dok_level_object *o;
    if(dok_controls_take(&world->actions,&action_commands)!=DOK_STATUS_OK)return 0;
    obs.next_weapon_total+=action_commands.next_weapon_pressed;
    if(!action_commands.use_triggered)return 1;
    ++obs.use_total;bits=action_commands.native_commands;
    if(!dok_render_trig((uint32_t)world->motion.heading,&sine,&cosine))return 0;
    /* Telemetry of the source one-point target. The public Use performs its
     * own checked calculation and is the only implementation changing bits. */
    {
        int64_t dx=-(int64_t)480*sine,dz=-(int64_t)480*cosine;
        x=world->motion.x+(dx>=0?(int32_t)(dx/32768):-(int32_t)((-dx+32767)/32768));
        z=world->motion.z-(dz>=0?(int32_t)(dz/32768):-(int32_t)((-dz+32767)/32768));
    }
    x/=480;z/=480;if(x<0)x=0;if(x>63)x=63;if(z<0)z=0;if(z>63)z=63;
    glyph=world->render.initial.maps[DOK_WORLD_TMAP][(uint32_t)z*64u+(uint32_t)x];
    index=world->render.effective.glyph_index[glyph];if(!index)return 0;
    o=&world->render.effective.objects[index-1u];
    native_begin(o->class_id==7u?0u:4u,glyph,bits,hero_seed->class_id,(uint16_t)obs.keys);
    if(dok_interactions_use(&world->interactions,world->render.initial.hero,&world->motion,
        &bits,(uint16_t)obs.keys,obs.c2)!=DOK_STATUS_OK)return 0;
    native_end(bits,NDS_INTERACTION_ACTION_USE);return 1;
}
static int stage_set(uint32_t stage)
{
    uint32_t now;
    if(dok_world_clock_read(&clock_state,&now)!=DOK_STATUS_OK)return 0;
    obs.stage_id=stage;++obs.stage_sequence;obs.stage_started=now;
    stage_callbacks=obs.callback_total;stage_sensor_entries=obs.sensor_entries;
    trace(NDS_INTERACTION_ACTION_STAGE);diagnostics();publish();return 1;
}
static int fixture_occupancy(uint16_t count)
{
    const dok_world_object_seed *seed=seed_for_glyph(obs.door_glyph);
    if(!seed || dok_collision_set_door_count(&world->collision,seed->reference,count)!=DOK_STATUS_OK)return 0;
    obs.fixture_occupancy=count;snapshot();trace(NDS_INTERACTION_ACTION_FIXTURE);return 1;
}
static int fixture_pose(int32_t x,int32_t z,int16_t heading)
{
    uint32_t clear,now,oldcell,newcell;
    int16_t angle=world->motion.angle_parameter;int32_t speed=world->motion.speed;
    if(x<238 || z<238 || x>=30720-238 || z>=30720-238 || heading<0 || heading>360)return 0;
    if(dok_collision_camera_clear(&world->collision,world->render.initial.hero,
        world->motion.x,world->motion.z,x,z,&clear)!=DOK_STATUS_OK || !clear)return 0;
    oldcell=(uint32_t)world->motion.z/480u*64u+(uint32_t)world->motion.x/480u;
    newcell=(uint32_t)z/480u*64u+(uint32_t)x/480u;
    if(world->render.initial.maps[DOK_WORLD_MMAP][oldcell]!=obs.hero_glyph)return 0;
    if(oldcell!=newcell) {
        uint32_t glyph=world->render.initial.maps[DOK_WORLD_TMAP][oldcell];
        const dok_level_object *o=definition_for_glyph(glyph);const dok_world_object_seed *s=seed_for_glyph(glyph);
        if(o && o->class_id==7u && world->collision.door_count[glyph]!=0u &&
           dok_collision_set_door_count(&world->collision,s->reference,(uint16_t)(world->collision.door_count[glyph]-1u))!=DOK_STATUS_OK)return 0;
        world->render.initial.maps[DOK_WORLD_MMAP][oldcell]=32;
        world->render.initial.maps[DOK_WORLD_MMAP][newcell]=(unsigned char)obs.hero_glyph;
        glyph=world->render.initial.maps[DOK_WORLD_TMAP][newcell];o=definition_for_glyph(glyph);s=seed_for_glyph(glyph);
        if(o && o->class_id==7u && dok_collision_set_door_count(&world->collision,s->reference,
           (uint16_t)(world->collision.door_count[glyph]+1u))!=DOK_STATUS_OK)return 0;
    }
    if(dok_motion_init(&world->motion,x,-300,z,heading,angle,speed)!=DOK_STATUS_OK ||
       dok_world_clock_read(&clock_state,&now)!=DOK_STATUS_OK)return 0;
    dok_camera_reset(&world->camera);
    if(dok_camera_commit(&world->camera,&world->motion,now)!=DOK_STATUS_OK)return 0;
    camera=world->camera.current.pose;watch_started=0;watch_start=0;service_elapsed=0;first_service=1;
    memset(&motion_report,0,sizeof(motion_report));memset(&collision_report,0,sizeof(collision_report));
    snapshot();trace(NDS_INTERACTION_ACTION_FIXTURE);return 1;
}
static int has_callback(uint16_t callback)
{
    const dok_world_object_seed *seed=seed_for_glyph(obs.door_glyph);const dok_event_owner *owner;
    uint32_t i;
    if(!seed || dok_events_inspect_owner(&scheduler,seed->reference,&owner)!=DOK_STATUS_OK)return 0;
    for(i=0;i<owner->count;++i)if(owner->events[i].callback==callback)return 1;
    return 0;
}
static int stage_progress(void)
{
    const dok_interaction_door *d=&world->interactions.doors[obs.door_glyph];
    if(obs.stage_id==NDS_INTERACTION_WAIT_LOCKED && obs.locked_total>locked_baseline) {
        obs.keys=obs.case_id==NDS_INTERACTION_CASE_LOCKED_A?2u:8u;
        snapshot();trace(NDS_INTERACTION_ACTION_FIXTURE);return stage_set(NDS_INTERACTION_WAIT_KEY_USE);
    }
    if((obs.stage_id==NDS_INTERACTION_WAIT_KEY_USE || obs.stage_id==NDS_INTERACTION_WAIT_USE) && has_callback(DOK_INTERACTION_OPEN))
        return stage_set(NDS_INTERACTION_OPENING);
    if(obs.stage_id==NDS_INTERACTION_OPENING && d->state==1u && d->ready) {
        if(!fixture_occupancy(1))return 0;
        return stage_set(NDS_INTERACTION_OPEN_OCCUPIED);
    }
    if(obs.stage_id==NDS_INTERACTION_OPEN_OCCUPIED && obs.case_id!=NDS_INTERACTION_CASE_MANUAL_H && blocked_auto)
        return stage_set(NDS_INTERACTION_AUTO_BLOCKED);
    if(obs.stage_id==NDS_INTERACTION_WAIT_CLOSE_USE && blocked_close)
        return stage_set(NDS_INTERACTION_CLOSE_BLOCKED);
    if(obs.stage_id==NDS_INTERACTION_AUTO_WAIT && d->state==1u && !d->ready && d->phase<d->extent-1)
        return stage_set(NDS_INTERACTION_CLOSING);
    if(obs.stage_id==NDS_INTERACTION_CLOSING && d->state==2u && d->ready && d->phase==0)
        return stage_set(NDS_INTERACTION_CLOSED);
    if(obs.stage_id==NDS_INTERACTION_FINISH_WAIT && obs.finish_total>finish_baseline)
        return stage_set(NDS_INTERACTION_FINISH_SENT);
    return 1;
}
static int stage_advance(void)
{
    if(!advance_requested)return 1;
    advance_requested=0;++obs.stage_advances;
    switch(obs.stage_id) {
    case NDS_INTERACTION_AUTO_BLOCKED:
        if(!fixture_occupancy(0))return 0;
        return stage_set(NDS_INTERACTION_AUTO_WAIT);
    case NDS_INTERACTION_OPEN_OCCUPIED:return stage_set(NDS_INTERACTION_WAIT_CLOSE_USE);
    case NDS_INTERACTION_CLOSE_BLOCKED:
        if(!fixture_occupancy(0))return 0;
        return stage_set(NDS_INTERACTION_CLOSING);
    case NDS_INTERACTION_CLOSED:case NDS_INTERACTION_FINISH_SENT:case_done=1;return 1;
    case NDS_INTERACTION_SENSOR_FIRST:
        if(obs.sensor_entries!=stage_sensor_entries+1u)return 0;
        if(!fixture_pose(obs.level_id==2u?23280:28080,obs.level_id==2u?27600:24720,0))return 0;
        return stage_set(NDS_INTERACTION_SENSOR_SAME);
    case NDS_INTERACTION_SENSOR_SAME:
        if(obs.sensor_entries!=stage_sensor_entries)return 0;
        if(!fixture_pose(hero_seed->x,hero_seed->z,hero_seed->heading))return 0;
        return stage_set(NDS_INTERACTION_SENSOR_OUT);
    case NDS_INTERACTION_SENSOR_OUT:
        if(!fixture_pose(obs.level_id==2u?23280:27600,obs.level_id==2u?27120:24720,0))return 0;
        return stage_set(NDS_INTERACTION_SENSOR_REENTER);
    case NDS_INTERACTION_SENSOR_REENTER:
        if(obs.sensor_entries!=stage_sensor_entries+1u)return 0;
        if(obs.level_id==2u) {
            conveyor_callbacks=0;
            if(!fixture_pose(1200,1200,0))return 0;
            return stage_set(NDS_INTERACTION_CONVEYOR);
        }
        if(!fixture_pose(17520,9840,0))return 0;
        finish_baseline=obs.finish_total;return stage_set(NDS_INTERACTION_FINISH_WAIT);
    case NDS_INTERACTION_CONVEYOR:
        if(!conveyor_callbacks || !fixture_pose(12720,3600,0))return 0;
        finish_baseline=obs.finish_total;return stage_set(NDS_INTERACTION_FINISH_WAIT);
    case NDS_INTERACTION_FINISH_WAIT:
        if(obs.finish_total!=finish_baseline)return 0;
        obs.score=(uint32_t)(int32_t)world->render.initial.category99_total;
        snapshot();trace(NDS_INTERACTION_ACTION_FIXTURE);return 1;
    default:return 0;
    }
}
static void diagnostic_line(const char *format,...)
{
    char text[96];va_list arguments;
    va_start(arguments,format);(void)vsnprintf(text,sizeof(text),format,arguments);va_end(arguments);
    /* Fixed-width overwrite avoids clearing an actively scanned console. */
    printf("%-31.31s\n",text);
}
static void diagnostics(void)
{
    consoleSelect(&bottom);consoleSetCursor(&bottom,0,0);
    diagnostic_line("N11 INTERACTIONS / CASE %lu",(unsigned long)obs.case_id);
    diagnostic_line("FIXTURE keys/pose/occupancy");diagnostic_line("stage %lu seq %lu",(unsigned long)obs.stage_id,(unsigned long)obs.stage_sequence);
    diagnostic_line("checks %lu failures %lu",(unsigned long)obs.checks,(unsigned long)obs.failures);
    diagnostic_line("x %ld z %ld h %d",(long)world->motion.x,(long)world->motion.z,(int)world->motion.heading);
    diagnostic_line("C2 %lu keys %lu score %ld",(unsigned long)obs.c2,(unsigned long)obs.keys,(long)(int32_t)obs.score);
    diagnostic_line("door %lu phase %ld/%ld",(unsigned long)obs.door_glyph,
        (long)(int32_t)world->interactions.doors[obs.door_glyph].phase,(long)(int32_t)world->interactions.doors[obs.door_glyph].extent);
    diagnostic_line("Start: pause; X/B: Use");diagnostic_line("A: advance when waiting");
    diagnostic_line("Locked %lu finish %lu",(unsigned long)obs.locked_total,(unsigned long)obs.finish_total);
    diagnostic_line("SDK callbacks / source C2");
    diagnostic_line("%s",obs.status==NDS_INTERACTION_PAUSED?"PAUSED":
        obs.status==NDS_INTERACTION_COMPLETE?"COMPLETE":obs.status==NDS_INTERACTION_FAILED?"FAILED":"LIVE");
    diagnostic_line("Actors N12 / map N20");
    diagnostic_line("Physical DS: N24");
}
#ifndef DOK_NDS_RENDER_AUDIT
#define DOK_NDS_RENDER_AUDIT 1
#endif
#if DOK_NDS_RENDER_AUDIT
static uint32_t collision_hash(void)
{
    uint32_t h=2166136261u,i,j;
    for(i=0;i<4u;++i)for(j=0;j<4096u;++j)h=(h^world->render.initial.maps[i][j])*16777619u;
    for(i=0;i<256u;++i){h=(h^world->collision.flags[i])*16777619u;
        const dok_interaction_door *d=&world->interactions.doors[i];
        h=(h^world->collision.door_count[i])*16777619u;h=(h^world->collision.known[i])*16777619u;
        h=(h^(uint16_t)d->phase)*16777619u;h=(h^(uint16_t)d->extent)*16777619u;
        h=(h^d->state)*16777619u;h=(h^d->ready)*16777619u;h=(h^d->marker)*16777619u;}
    h=(h^world->interactions.revision)*16777619u;
    {const unsigned char *bytes=(const unsigned char *)(const void *)bank;
        for(i=0;i<sizeof(*bank);++i)h=(h^bytes[i])*16777619u;}
    return h;
}
#endif
static int render_one(void)
{
    uint32_t now,heading,interval,elapsed;dok_nds_render_camera view;
#if DOK_NDS_RENDER_AUDIT
    uint32_t before_hash=collision_hash();
#endif
    dok_nds_render_stats stats={0};dok_door_render_counts door_counts;const dok_nds_render_frame *frame=NULL;
    dok_motion_state before=world->motion;dok_camera_history history_before=world->camera;
    if(dok_interactions_phases(&world->interactions,&world->phases)!=DOK_STATUS_OK)return 0;
    camera_fallback=0;camera_admitted=0;camera_alpha=65536u;
    if(dok_world_clock_read(&clock_state,&now)!=DOK_STATUS_OK)return 0;
    if(world->camera.count>=2u){
        interval=world->camera.current.tick-world->camera.previous.tick;
        elapsed=now-world->camera.current.tick;
        if(interval!=0u&&elapsed<interval)camera_alpha=(uint32_t)(((uint64_t)elapsed<<16)/interval);
        if(dok_camera_interpolate(&world->camera,camera_alpha,&camera)!=DOK_STATUS_OK)return 0;
    }else camera=world->camera.current.pose;
    if(dok_collision_camera_clear(&world->collision,world->render.initial.hero,
       world->motion.x,world->motion.z,camera.x,camera.z,&camera_admitted)!=DOK_STATUS_OK)return 0;
    if(!camera_admitted){camera=world->camera.current.pose;camera_fallback=1;}
    heading=dok_camera_heading(&camera);view=(dok_nds_render_camera){camera.x,camera.y,camera.z,0,0};
    if(!dok_render_trig(heading,&view.sin_q15,&view.cos_q15)||
       dok_door_scene_prepare(scene,&world->render.initial,world->render.bindings,&view,
           heading,&world->phases,&door_counts,&frame)!=DOK_RENDER_SCENE_OK){++obs.matrix_errors;return 0;}
    if(dok_nds_render_submit(frame,&stats)!=DOK_NDS_RENDER_OK){
        (void)dok_render_scene_release(scene,scene->generation);++obs.render_overflows;return 0;}
    if(dok_render_scene_release(scene,scene->generation)!=DOK_RENDER_SCENE_OK)return 0;
    swiWaitForVBlank();dok_nds_render_present();dok_nds_render_observe(&stats);
    if(stats.disp3d_flags||stats.gxstat_flags){++obs.render_overflows;return 0;}
    if(memcmp(&before,&world->motion,sizeof(before))||
       memcmp(&history_before,&world->camera,sizeof(history_before))||
       world->phases.revision!=world->interactions.revision)return 0;
#if DOK_NDS_RENDER_AUDIT
    /* Expensive full-world/queue hashing belongs to the audit build. The
     * interactive probe retains motion/camera/revision guards and external
     * snapshot/x86 verification, without scanning ~100 KiB per new frame. */
    if(before_hash!=collision_hash())return 0;
#endif
    obs.rendered_doors=door_counts.doors;obs.rendered_cropped=door_counts.cropped;obs.rendered_hidden=door_counts.hidden;
    ++obs.render_reads;++obs.presented_frames;
    if(obs.status!=NDS_INTERACTION_PAUSED){++obs.frame_id;++obs.total_frames;}
    trace(NDS_INTERACTION_ACTION_RENDER);return 1;
}
static int level_begin(void)
{
    const dok_level_field *speed,*angle;const dok_level_object *definition;
    uint32_t i,now;
    dok_event_config config={&clock_state,dok_world_clock_read,NULL,event_callback,obs.world_epoch};
    dok_interaction_services interaction_services={NULL,interaction_sink,NULL,interaction_move,NULL,trig};
    for(i=0;i<world->render.initial.object_count;++i)
        if(dok_object_ref_equal(world->render.initial.objects[i].reference,world->render.initial.hero))
            hero_seed=&world->render.initial.objects[i];
    if(!hero_seed)return 0;
    definition=&world->render.effective.objects[hero_seed->definition_index];
    speed=dok_level_field_at(definition,0x38u);angle=dok_level_field_at(definition,0x34u);
    if(!speed||!angle)return 0;
    obs.hero_id=hero_seed->reference.object_id;obs.hero_generation=hero_seed->reference.generation;
    obs.hero_glyph=definition->glyph;
    if(dok_world_clock_init(&clock_state,&platform,0)!=DOK_STATUS_OK||
       dok_input_buffer_initialize(&world->input)!=DOK_STATUS_OK||
       dok_controls_initialize(&world->controls)!=DOK_STATUS_OK||
       dok_controls_initialize(&world->actions)!=DOK_STATUS_OK||
       dok_motion_init(&world->motion,hero_seed->x,hero_seed->y,hero_seed->z,hero_seed->heading,
           (int16_t)angle->value,speed->value)!=DOK_STATUS_OK||
       dok_events_init(&scheduler,bank,&config)!=DOK_STATUS_OK||
       dok_events_register(&scheduler,hero_seed->reference,4u)!=DOK_STATUS_OK||
       dok_interactions_bind(&world->interactions,&world->render.initial,&world->collision,&scheduler,&interaction_services)!=DOK_STATUS_OK||
       dok_interactions_register(&world->interactions)!=DOK_STATUS_OK||
       dok_events_schedule(&scheduler,hero_seed->reference,1u,0u,416u)!=DOK_STATUS_OK||
       dok_world_clock_read(&clock_state,&now)!=DOK_STATUS_OK)return 0;
    watch_started=0;watch_start=0;service_elapsed=0;first_service=1;service_tick=0;
    native_deadline=0;native_interval=416;input_flags=0;
    obs.c2=4;obs.keys=0;
    obs.score=(uint32_t)(int32_t)(int16_t)(world->render.initial.category99_total-1);
    obs.fixture_occupancy=0;
    obs.door_glyph=obs.case_id==1u?97u:obs.case_id==2u?100u:obs.case_id==3u?103u:obs.case_id==6u?107u:104u;
    obs.sweep_ticks=0;obs.sweep_begin=0;obs.sweep_end=0;sweep_ready=0;sweep_paused=0;
    case_done=0;advance_requested=0;blocked_auto=0;blocked_close=0;conveyor_callbacks=0;
    locked_baseline=obs.locked_total;finish_baseline=obs.finish_total;native_active=0;
    memset(&commands,0,sizeof(commands));memset(&motion_report,0,sizeof(motion_report));
    memset(&collision_report,0,sizeof(collision_report));
    dok_camera_reset(&world->camera);
    if(dok_camera_commit(&world->camera,&world->motion,now)!=DOK_STATUS_OK)return 0;
    camera=world->camera.current.pose;camera_alpha=65536;camera_admitted=1;camera_fallback=0;
    snapshot();trace(NDS_INTERACTION_ACTION_LEVEL_BEGIN);
    if(!poll_input())return 0;
    obs.status=NDS_INTERACTION_LIVE;
    if(obs.case_id<=4u) {
        if(!stage_set(obs.case_id==1u || obs.case_id==4u?NDS_INTERACTION_WAIT_LOCKED:NDS_INTERACTION_WAIT_USE))return 0;
        if(obs.case_id==1u)return fixture_pose(27360,11280,0);
        if(obs.case_id==2u)return fixture_pose(11280,22560,270);
        if(obs.case_id==3u)return fixture_pose(1920,11280,0);
        return fixture_pose(7920,22800,0);
    }
    if(!stage_set(NDS_INTERACTION_SENSOR_FIRST))return 0;
    return fixture_pose(obs.level_id==2u?23280:27600,obs.level_id==2u?27120:24720,0);
}
int main(void)
{
    dok_interaction_test_report unit={0};dok_door_render_test_report door_unit={0};
    dok_interaction_router_report router={0};dok_event_dispatch_report dispatch_report;uint32_t case_id;
    obs.trace_address=address((const void *)g_nds_interaction_trace);
    obs.trace_capacity=NDS_INTERACTION_TRACE_CAPACITY;obs.trace_record_bytes=sizeof(NdsInteractionTraceRecord);
    obs.snapshot_address=address((const void *)g_nds_interaction_snapshot);
    obs.snapshot_capacity=NDS_INTERACTION_SNAPSHOT_CAPACITY;obs.snapshot_record_bytes=sizeof(NdsInteractionSnapshot);
    if(!memory_setup()){obs.error_bits=1;goto done;}
    irqSet(IRQ_VBLANK,vblank_irq);irqEnable(IRQ_VBLANK);
    cpuStartTiming(0);
    if(dok_nds_render_init()!=DOK_NDS_RENDER_OK){obs.error_bits=2;goto done;}
    hud_bg=bgInit(3,BgType_Bmp8,BgSize_B8_256x256,0,0);bgSetPriority(hud_bg,0);
    REG_BG0CNT=(REG_BG0CNT&~3u)|1u;
    consoleInit(&bottom,0,BgType_Text4bpp,BgSize_T_256x256,31,0,false,true);console_ready=1;publish();
    (void)dok_interaction_run_cases(&world_storage->unit,&unit);
    obs.interaction_checks=unit.checks;obs.checks=unit.checks;obs.failures=unit.failures;
    obs.first_failed_line=unit.first_failed_line;
    (void)dok_door_render_run_cases(&scratch->unit,&door_unit);
    obs.door_render_checks=door_unit.checks;obs.checks+=door_unit.checks;obs.failures+=door_unit.failures;
    if(!obs.first_failed_line)obs.first_failed_line=door_unit.first_failed_line;
    (void)dok_interaction_router_run_cases(&world_storage->router,&router);
    obs.replay_checks=router.checks;obs.checks+=router.checks;obs.failures+=router.failures;
    if(!obs.first_failed_line)obs.first_failed_line=router.first_failed_line;
    memset(world,0,sizeof(*world));
    if(obs.failures){obs.error_bits=4;goto done;}
    if(!package_open()||dok_nds_clock_initialize(&platform)!=DOK_STATUS_OK||
       dok_nds_input_initialize(&sdk_input,&platform)!=DOK_STATUS_OK){obs.error_bits=8;goto done;}
    obs.counter_hz_numerator=platform.counter_hz_numerator;obs.counter_hz_denominator=platform.counter_hz_denominator;
    for(case_id=1;case_id<=6u;++case_id){
        uint32_t level=case_id<=3u?1u:case_id<=5u?2u:3u;
        obs.case_id=case_id;
        if(!load_world(level)||!level_begin()){obs.error_bits|=8;break;}
        while(!case_done){
            uint32_t previous_status=obs.status;
            /* Continuous native boundaries: telemetry after the last end
             * belongs to the following sweep instead of disappearing from C2. */
            if(sweep_ready)sweep_start=sweep_finish;
            else if(dok_world_clock_read(&clock_state,&sweep_start)!=DOK_STATUS_OK){obs.error_bits|=16;break;}
            if(!poll_input()){obs.error_bits|=16;break;}
            if(obs.status!=previous_status)diagnostics();
            if(obs.status!=NDS_INTERACTION_PAUSED) {
                if(sweep_ready && !sweep_paused && obs.sweep_ticks &&
                   dok_interactions_cadence(obs.c2,obs.sweep_ticks,&obs.c2)!=DOK_STATUS_OK){obs.error_bits|=16;break;}
                if(!stage_advance()){obs.error_bits|=16;break;}
                if(case_done)break;
                if(!action_service() || dok_events_dispatch(&scheduler,&dispatch_report)!=DOK_STATUS_OK || !stage_progress()){
                    obs.error_bits|=16;break;
                }
            }
            if(obs.error_bits)break;
            /* Present every service sweep. The renderer already waits for
             * VBlank; an extra blank sweep imposed an artificial FPS cap.
             * Source C2 still measures the actual complete sweep below. */
            if(!render_one()){obs.error_bits|=32;break;}
            if(dok_world_clock_read(&clock_state,&sweep_finish)!=DOK_STATUS_OK){obs.error_bits|=16;break;}
            obs.sweep_begin=sweep_start;obs.sweep_end=sweep_finish;obs.sweep_ticks=sweep_finish-sweep_start;
            ++obs.sweep_total;sweep_ready=1;sweep_paused=obs.status==NDS_INTERACTION_PAUSED;
            trace(NDS_INTERACTION_ACTION_SWEEP);
            if(obs.input_total%120u==0u)diagnostics();
            publish();
        }
        if(obs.error_bits)break;
        trace(NDS_INTERACTION_ACTION_LEVEL_END);publish();
    }
    if(!memory_check())obs.error_bits|=64;
 done:
    obs.status=obs.error_bits?NDS_INTERACTION_FAILED:NDS_INTERACTION_COMPLETE;
    if(world&&console_ready)diagnostics();
    if(world&&clock_state._initialized)trace(NDS_INTERACTION_ACTION_COMPLETE);
    publish();
    for(;;){swiWaitForVBlank();publish();}
}
