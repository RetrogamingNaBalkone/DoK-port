/* N10 collision diagnostic: three independent RU level probes, no saves. */
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
#include "collision_cases.h"
#include "collision_routes.h"
#include "collision_probe.h"

typedef struct CollisionLive {
    dok_render_world render;
    dok_input_buffer input;
    dok_control_latch controls;
    dok_motion_state motion;
    dok_camera_history camera;
    dok_collision_state collision;
    dok_collision_context movement;
} CollisionLive;
typedef union CollisionWorld {
    CollisionLive live;
    dok_collision_test_workspace unit;
} CollisionWorld;
typedef union CollisionScratch {
    dok_render_workspace render;
    dok_collision_routes_workspace routes;
} CollisionScratch;
typedef union CollisionIO {
    dok_asset_workspace catalog;
    unsigned char fixture[65536];
} CollisionIO;
_Static_assert(sizeof(CollisionWorld)<=DOK_NDS_WORLD_BYTES,"N10 world slab");
_Static_assert(sizeof(CollisionScratch)<=DOK_NDS_SCRATCH_BYTES,"N10 scratch slab");
_Static_assert(sizeof(CollisionIO)<=DOK_NDS_IO_BYTES,"N10 I/O slab");
extern unsigned char __end__, __eheap_end;
_Alignas(32) volatile NdsCollisionProbe g_nds_collision_probe;
_Alignas(32) volatile NdsCollisionTraceRecord g_nds_collision_trace[NDS_COLLISION_TRACE_CAPACITY];
_Alignas(32) volatile NdsCollisionSnapshot g_nds_collision_snapshot[NDS_COLLISION_SNAPSHOT_CAPACITY];
static NdsCollisionProbe obs;
static volatile uint32_t vblanks;
static unsigned char *managed,*assets;
static CollisionWorld *world_storage;
static CollisionLive *world;
static CollisionScratch *scratch;
static CollisionIO *io;
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
static dok_motion_report motion_report;
static dok_collision_report collision_report;
static const dok_world_object_seed *hero_seed;
static uint32_t camera_alpha,camera_fallback,camera_admitted;
static uint32_t latest_snapshot_ordinal,latest_snapshot_slot;
static uint32_t watch_started,watch_start,service_elapsed,first_service;
static uint32_t native_deadline,native_interval,input_flags,service_tick;
static NdsAssetFile package;
static dok_asset_session session;
static dok_nds_render_texture textures[DOK_NDS_RENDER_TEXTURE_CAPACITY];
static uint32_t fixture_count;
static int hud_bg,console_ready;
static PrintConsole bottom;

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
    obs.magic=NDS_COLLISION_PROBE_MAGIC;obs.abi_version=NDS_COLLISION_ABI_VERSION;
    obs.size_bytes=sizeof(obs);obs.sequence=g_nds_collision_probe.sequence+1u;
    g_nds_collision_probe.sequence=obs.sequence;
    DC_FlushRange((const void *)&g_nds_collision_probe.sequence,32);
    obs.vblank_count=vblanks;
    memcpy((void *)&g_nds_collision_probe,&obs,sizeof(obs));
    DC_FlushRange((const void *)&g_nds_collision_probe,sizeof(obs));
    ++g_nds_collision_probe.sequence;
    DC_FlushRange((const void *)&g_nds_collision_probe.sequence,32);
}
static void trace(uint32_t action)
{
    NdsCollisionTraceRecord row={0};
    const dok_motion_state *m=&world->motion;
    uint32_t index=obs.trace_total%NDS_COLLISION_TRACE_CAPACITY;
    clock_probe();
    row.magic=NDS_COLLISION_TRACE_MAGIC;row.abi_version=NDS_COLLISION_ABI_VERSION;
    row.size_bytes=sizeof(row);row.sequence=g_nds_collision_trace[index].sequence+1u;
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
    if(action==NDS_COLLISION_ACTION_INPUT) {
        row.physical_held=filtered_input.held;row.physical_pressed=filtered_input.pressed;
        row.physical_released=filtered_input.released;
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
    if(action==NDS_COLLISION_ACTION_LEVEL_BEGIN||action==NDS_COLLISION_ACTION_CALLBACK){
        row.snapshot_ordinal=latest_snapshot_ordinal;row.snapshot_slot=latest_snapshot_slot;
    }
    g_nds_collision_trace[index].sequence=row.sequence;
    DC_FlushRange((const void *)&g_nds_collision_trace[index].sequence,32);
    memcpy((void *)&g_nds_collision_trace[index],&row,sizeof(row));
    DC_FlushRange((const void *)&g_nds_collision_trace[index],sizeof(row));
    ++g_nds_collision_trace[index].sequence;
    DC_FlushRange((const void *)&g_nds_collision_trace[index].sequence,32);
}

static void snapshot(void)
{
    NdsCollisionSnapshotHeader h={0};
    uint32_t index=obs.snapshot_total%NDS_COLLISION_SNAPSHOT_CAPACITY;
    h.magic=NDS_COLLISION_SNAPSHOT_MAGIC;h.abi_version=NDS_COLLISION_ABI_VERSION;
    h.size_bytes=sizeof(NdsCollisionSnapshot);
    h.sequence=g_nds_collision_snapshot[index].header.sequence+1u;
    h.ordinal=++obs.snapshot_total;h.level_id=obs.level_id;h.world_epoch=obs.world_epoch;
    h.callback_total=obs.callback_total;h.trace_ordinal=obs.trace_total+1u;
    h.width=(uint16_t)world->render.initial.definition->settings[DOK_LEVEL_WIDTH];
    h.length=(uint16_t)world->render.initial.definition->settings[DOK_LEVEL_LENGTH];
    h.hero_glyph=obs.hero_glyph;h.hero_id=obs.hero_id;h.hero_generation=obs.hero_generation;
    g_nds_collision_snapshot[index].header.sequence=h.sequence;
    DC_FlushRange((const void *)&g_nds_collision_snapshot[index].header.sequence,32);
    memcpy((void *)&g_nds_collision_snapshot[index].maps,&world->render.initial.maps,sizeof(world->render.initial.maps));
    memcpy((void *)&g_nds_collision_snapshot[index].flags,&world->collision.flags,sizeof(world->collision.flags));
    memcpy((void *)&g_nds_collision_snapshot[index].door_count,&world->collision.door_count,sizeof(world->collision.door_count));
    memcpy((void *)&g_nds_collision_snapshot[index].known,&world->collision.known,sizeof(world->collision.known));
    memcpy((void *)&g_nds_collision_snapshot[index].header,&h,sizeof(h));
    DC_FlushRange((const void *)&g_nds_collision_snapshot[index],sizeof(NdsCollisionSnapshot));
    ++g_nds_collision_snapshot[index].header.sequence;
    DC_FlushRange((const void *)&g_nds_collision_snapshot[index].header.sequence,32);
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
    world_storage=(CollisionWorld *)(void *)(managed+cursor);world=&world_storage->live;cursor+=DOK_NDS_WORLD_BYTES;
    bank=(dok_event_bank *)(void *)(managed+cursor);cursor+=DOK_NDS_EVENTS_BYTES;
    scratch=(CollisionScratch *)(void *)(managed+cursor);cursor+=DOK_NDS_SCRATCH_BYTES;
    scene=(dok_render_scene *)(void *)(managed+cursor);cursor+=DOK_NDS_SCENE_BYTES;
    assets=managed+cursor;cursor+=DOK_NDS_ASSETS_BYTES;cursor+=DOK_NDS_AUDIO_BYTES;
    io=(CollisionIO *)(void *)(managed+cursor);cursor+=DOK_NDS_IO_BYTES;
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
    dok_nds_render_stats stats={0};dok_collision_routes_report routes={0};uint32_t i;
    obs.status=NDS_COLLISION_LOADING;obs.level_id=level;obs.world_epoch=110u+level;
    obs.frame_id=0;obs.level_callback_total=0;publish();
    memset(world,0,sizeof(*world));hero_seed=NULL;
    if(dok_render_world_load(&session,level,obs.world_epoch,&world->render,&scratch->render,assets,
        DOK_NDS_ASSETS_BYTES)!=DOK_STATUS_OK||
       dok_collision_bind(&world->collision,&world->render.initial)!=DOK_STATUS_OK)return 0;
    if(dok_collision_routes_run(&routes,&world->render.initial,&world->collision,bank,&scratch->routes)!=0){
        obs.failures+=routes.failures;obs.first_failed_line=routes.first_failed_line;return 0;
    }
    obs.replay_checks+=routes.checks;obs.checks+=routes.checks;
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
    world->motion.flags=(world->motion.flags&~UINT32_C(0x00182a3c))|commands.native_commands;
    input_flags=world->motion.flags;
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
    obs.use_total+=commands.use_triggered;obs.next_weapon_total+=commands.next_weapon_pressed;
    obs.collision_requested+=collision_report.requested;obs.collision_committed+=collision_report.committed;
    obs.collision_adjusted+=collision_report.adjusted;obs.collision_rejected+=collision_report.rejected;
    obs.collision_reason_mask|=collision_report.reason_mask;
    snapshot();trace(NDS_COLLISION_ACTION_CALLBACK);
}
static int ui_boundary(void)
{ return dok_input_ui_boundary(&world->input)==DOK_STATUS_OK &&
         dok_controls_ui_boundary(&world->controls)==DOK_STATUS_OK; }
static int poll_input(void)
{
    dok_input_sample filtered;uint32_t available;
    if(dok_nds_input_poll(&sdk_input,&raw)!=DOK_STATUS_OK ||
       dok_input_capture(&world->input,&raw.frame,raw.counter)!=DOK_STATUS_OK) return 0;
    /* Read-only telemetry of the exact captured sample, before any drain or
     * UI boundary. Core owns the queue and remains its only writer. */
    filtered_input=world->input._samples[(world->input._head+world->input._count-1u)%DOK_INPUT_SAMPLE_LIMIT].frame;
    ++obs.input_total;obs.buttons_seen|=raw.frame.held|raw.frame.pressed;
    obs.touch_seen|=raw.frame.touch_flags;trace(NDS_COLLISION_ACTION_INPUT);
    if(obs.status==NDS_COLLISION_LOADING)return ui_boundary();
    if(raw.frame.pressed&DOK_BUTTON_START) {
        if(obs.status==NDS_COLLISION_PAUSED) {
            if(!ui_boundary() || dok_world_clock_resume(&clock_state)!=DOK_STATUS_OK) return 0;
            obs.status=NDS_COLLISION_LIVE;++obs.resume_count;trace(NDS_COLLISION_ACTION_RESUME);
        } else {
            if(dok_world_clock_pause(&clock_state)!=DOK_STATUS_OK || !ui_boundary()) return 0;
            obs.status=NDS_COLLISION_PAUSED;++obs.pause_count;trace(NDS_COLLISION_ACTION_PAUSE);
        }
        return 1;
    }
    if(obs.status==NDS_COLLISION_PAUSED) return ui_boundary();
    do {
        if(dok_input_pop_through(&world->input,raw.counter,&filtered,&available)!=DOK_STATUS_OK) return 0;
        if(available && dok_controls_feed(&world->controls,&filtered.frame)!=DOK_STATUS_OK) return 0;
    } while(available);
    return 1;
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
    diagnostic_line("N10 COLLISION / LEVEL %lu",(unsigned long)obs.level_id);
    diagnostic_line("Walls / occupancy / sliding");diagnostic_line("");
    diagnostic_line("checks %lu failures %lu",(unsigned long)obs.checks,(unsigned long)obs.failures);
    diagnostic_line("x %ld z %ld h %d",(long)world->motion.x,(long)world->motion.z,(int)world->motion.heading);
    diagnostic_line("services %lu frames %lu",(unsigned long)obs.callback_total,(unsigned long)obs.frame_id);
    diagnostic_line("Start: pause/resume");diagnostic_line("Dpad: move / strafe");
    diagnostic_line("Y A: turn; L: run");diagnostic_line("R: fire cmd; X B: Use cmd");
    diagnostic_line("Select: next weapon cmd");
    diagnostic_line("%s",obs.status==NDS_COLLISION_PAUSED?"PAUSED":
        obs.status==NDS_COLLISION_COMPLETE?"COMPLETE":obs.status==NDS_COLLISION_FAILED?"FAILED":"LIVE");
    diagnostic_line("Actors N12 / map N20");
    diagnostic_line("Physical DS: N24");
}
static uint32_t collision_hash(void)
{
    uint32_t h=2166136261u,i,j;
    for(i=0;i<4u;++i)for(j=0;j<4096u;++j)h=(h^world->render.initial.maps[i][j])*16777619u;
    for(i=0;i<256u;++i){h=(h^world->collision.flags[i])*16777619u;
        h=(h^world->collision.door_count[i])*16777619u;h=(h^world->collision.known[i])*16777619u;}
    return h;
}
static int render_one(void)
{
    uint32_t now,heading,interval,elapsed,before_hash;dok_nds_render_camera view;
    dok_nds_render_stats stats={0};const dok_nds_render_frame *frame=NULL;
    dok_motion_state before=world->motion;dok_camera_history history_before=world->camera;
    before_hash=collision_hash();camera_fallback=0;camera_admitted=0;camera_alpha=65536u;
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
       dok_render_scene_prepare(scene,&world->render.initial,world->render.bindings,&view,
           heading,&frame)!=DOK_RENDER_SCENE_OK){++obs.matrix_errors;return 0;}
    if(dok_nds_render_submit(frame,&stats)!=DOK_NDS_RENDER_OK){++obs.render_overflows;return 0;}
    if(dok_render_scene_release(scene,scene->generation)!=DOK_RENDER_SCENE_OK)return 0;
    swiWaitForVBlank();dok_nds_render_present();dok_nds_render_observe(&stats);
    if(stats.disp3d_flags||stats.gxstat_flags){++obs.render_overflows;return 0;}
    if(memcmp(&before,&world->motion,sizeof(before))||
       memcmp(&history_before,&world->camera,sizeof(history_before))||before_hash!=collision_hash())return 0;
    ++obs.render_reads;++obs.presented_frames;
    if(obs.status!=NDS_COLLISION_PAUSED){++obs.frame_id;++obs.total_frames;}
    trace(NDS_COLLISION_ACTION_RENDER);return 1;
}
static int level_begin(void)
{
    const dok_level_field *speed,*angle;const dok_level_object *definition;
    uint32_t i,now;
    dok_event_config config={&clock_state,dok_world_clock_read,NULL,hero_callback,obs.world_epoch};
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
       dok_motion_init(&world->motion,hero_seed->x,hero_seed->y,hero_seed->z,hero_seed->heading,
           (int16_t)angle->value,speed->value)!=DOK_STATUS_OK||
       dok_events_init(&scheduler,bank,&config)!=DOK_STATUS_OK||
       dok_events_register(&scheduler,hero_seed->reference,4u)!=DOK_STATUS_OK||
       dok_events_schedule(&scheduler,hero_seed->reference,1u,0u,416u)!=DOK_STATUS_OK||
       dok_world_clock_read(&clock_state,&now)!=DOK_STATUS_OK)return 0;
    watch_started=0;watch_start=0;service_elapsed=0;first_service=1;service_tick=0;
    native_deadline=0;native_interval=416;input_flags=0;
    memset(&commands,0,sizeof(commands));memset(&motion_report,0,sizeof(motion_report));
    memset(&collision_report,0,sizeof(collision_report));
    dok_camera_reset(&world->camera);
    if(dok_camera_commit(&world->camera,&world->motion,now)!=DOK_STATUS_OK)return 0;
    camera=world->camera.current.pose;camera_alpha=65536;camera_admitted=1;camera_fallback=0;
    snapshot();trace(NDS_COLLISION_ACTION_LEVEL_BEGIN);
    if(!poll_input())return 0;
    obs.status=NDS_COLLISION_LIVE;publish();diagnostics();return 1;
}
int main(void)
{
    dok_collision_test_report unit={0};dok_event_dispatch_report dispatch_report;uint32_t level;
    obs.trace_address=address((const void *)g_nds_collision_trace);
    obs.trace_capacity=NDS_COLLISION_TRACE_CAPACITY;obs.trace_record_bytes=sizeof(NdsCollisionTraceRecord);
    obs.snapshot_address=address((const void *)g_nds_collision_snapshot);
    obs.snapshot_capacity=NDS_COLLISION_SNAPSHOT_CAPACITY;obs.snapshot_record_bytes=sizeof(NdsCollisionSnapshot);
    if(!memory_setup()){obs.error_bits=1;goto done;}
    irqSet(IRQ_VBLANK,vblank_irq);irqEnable(IRQ_VBLANK);
    cpuStartTiming(0);
    if(dok_nds_render_init()!=DOK_NDS_RENDER_OK){obs.error_bits=2;goto done;}
    hud_bg=bgInit(3,BgType_Bmp8,BgSize_B8_256x256,0,0);bgSetPriority(hud_bg,0);
    REG_BG0CNT=(REG_BG0CNT&~3u)|1u;
    consoleInit(&bottom,0,BgType_Text4bpp,BgSize_T_256x256,31,0,false,true);console_ready=1;publish();
    (void)dok_collision_run_cases(&world_storage->unit,&unit);
    obs.collision_checks=unit.checks;obs.checks=unit.checks;obs.failures=unit.failures;
    obs.first_failed_line=unit.first_failed_line;
    memset(world,0,sizeof(*world));
    if(obs.failures){obs.error_bits=4;goto done;}
    if(!package_open()||dok_nds_clock_initialize(&platform)!=DOK_STATUS_OK||
       dok_nds_input_initialize(&sdk_input,&platform)!=DOK_STATUS_OK){obs.error_bits=8;goto done;}
    obs.counter_hz_numerator=platform.counter_hz_numerator;obs.counter_hz_denominator=platform.counter_hz_denominator;
    for(level=1;level<=3u;++level){
        uint32_t resume_before=obs.resume_count,pause_before=obs.pause_count;
        if(!load_world(level)||!level_begin()){obs.error_bits|=8;break;}
        while(obs.frame_id<200u||obs.resume_count<=resume_before||obs.pause_count<=pause_before){
            uint32_t previous_status=obs.status;
            if(!poll_input()){obs.error_bits|=16;break;}
            if(obs.status!=previous_status)diagnostics();
            if(obs.status!=NDS_COLLISION_PAUSED&&
               dok_events_dispatch(&scheduler,&dispatch_report)!=DOK_STATUS_OK){obs.error_bits|=16;break;}
            if(obs.error_bits||!render_one()){obs.error_bits|=32;break;}
            if(obs.input_total%60u==0u)diagnostics();
            publish();
        }
        if(obs.error_bits)break;
        trace(NDS_COLLISION_ACTION_LEVEL_END);publish();
    }
    if(!memory_check())obs.error_bits|=64;
 done:
    obs.status=obs.error_bits?NDS_COLLISION_FAILED:NDS_COLLISION_COMPLETE;
    if(world&&console_ready)diagnostics();
    if(world&&clock_state._initialized)trace(NDS_COLLISION_ACTION_COMPLETE);
    publish();
    for(;;){swiWaitForVBlank();publish();}
}
