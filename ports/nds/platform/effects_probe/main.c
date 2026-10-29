/* N13 combined LIGHT/water/palette and N12 object diagnostic. */
#include <nds.h>
#include <fat.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dok/render_fixture.h"
#include "dok/render_materials.h"
#include "dok/render_scene.h"
#include "dok/sha256.h"
#include "file_reader.h"
#include "render_backend.h"
#include "render_probe.h"
#include "sprites_probe.h"
#include "dok/sprite_assets.h"
#include "dok/texture_cache.h"
#include "dok/door_render.h"
#include "effects_runtime.h"
#include "clock.h"
#ifndef EFFECT_LIGHT_CACHE
#define EFFECT_LIGHT_CACHE 0
#endif
#if EFFECT_LIGHT_CACHE
#include "dok/light_cache.h"
_Alignas(32) static dok_light_cache geometry_cache;
#endif
#if EFFECT_GPU_FOG
#define EFFECT_POOL_BYTES (128u*1024u)
#else
#define EFFECT_POOL_BYTES DOK_EFFECT_CACHE_BYTES
#endif
#ifndef SPRITE_LANGUAGE
#define SPRITE_LANGUAGE 1
#endif
#ifndef SPRITE_AUDIT
#define SPRITE_AUDIT 0
#endif

typedef union ProbeIO {
    dok_asset_workspace catalog;
    unsigned char fixture[65536];
} ProbeIO;
_Static_assert(sizeof(ProbeIO)<=DOK_NDS_IO_BYTES,"N07 I/O region");
extern unsigned char __end__, __eheap_end;
_Alignas(32) volatile NdsRenderProbe g_nds_render_probe;
_Alignas(32) volatile NdsRenderHistoryRecord g_nds_render_history[NDS_RENDER_HISTORY_CAPACITY];
_Alignas(32) volatile NdsRenderPositionRecord g_nds_render_positions[NDS_RENDER_POSITION_CAPACITY];
static NdsRenderProbe obs;
static volatile uint32_t vblank_counter;
static unsigned char *managed, *assets;
typedef struct SpriteWorld {
    dok_render_world geometry;
    dok_sprite_asset_bank bank;
    dok_sprite_list list;
    dok_texture_cache cache;
    NdsEffectsRuntime effects;
} SpriteWorld;
_Static_assert(sizeof(SpriteWorld)<=DOK_NDS_WORLD_BYTES,"N12 world region");
static SpriteWorld *storage;
static dok_render_world *world;
_Alignas(32) volatile NdsSpriteProbe g_nds_sprite_probe;
_Alignas(32) volatile NdsSpriteTrace g_nds_sprite_trace[NDS_SPRITE_TRACE_CAPACITY];
static NdsSpriteProbe sprite_obs;
static dok_nds_stream_texture demand[DOK_NDS_RENDER_TEXTURE_CAPACITY];
static uint16_t sprite_slots[DOK_SPRITE_LIMIT];
static dok_nds_stream_stats stream_stats;
static uint32_t demand_count;
static uint32_t stream_ready;
static unsigned char world_digest[32];
static void hash_bytes(const void *bytes,uint32_t count,unsigned char hash[32])
{ dok_sha256 state;dok_sha256_begin(&state);dok_sha256_update(&state,bytes,count);dok_sha256_end(&state,hash); }
static dok_render_workspace *scratch;
static dok_render_scene *scene;
static ProbeIO *io;
static NdsAssetFile package;
static dok_asset_session session;
static dok_asset_reader reader;
static dok_event_bank *events;
static dok_platform_services platform;
static uint16_t raw_material[DOK_NDS_RENDER_TEXTURE_CAPACITY];
static dok_light_counts light_counts;
static uint32_t mask_identity_count;
_Alignas(32) volatile uint32_t g_nds_effect_probe[32];
/* Per-case worst preparation profile, diagnostic only; no skipped work. */
_Alignas(32) volatile uint32_t g_nds_effect_profile[64][8];

static dok_render_case cases[DOK_RENDER_FIXTURE_CASES];
static uint32_t fixture_bytes, fixture_count;
static uint32_t previous_present_ticks, previous_present_vblank;
static int hud_bg;
static PrintConsole bottom;
static dok_nds_render_stats last_render_stats;

static uint32_t address(const void *p) { return (uint32_t)(uintptr_t)p; }
static void vblank_irq(void) { ++vblank_counter; }
static uint32_t le32(const unsigned char *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static int32_t signed_word(uint32_t value)
{ return value<=INT32_MAX?(int32_t)value:-1-(int32_t)(UINT32_MAX-value); }

/* Both sequence start and frame presentation use this same sampled boundary.
 * Post-boundary layer enable and telemetry belong to the following interval. */
static __attribute__((noinline)) uint32_t wait_presentation_boundary(void)
{
    swiWaitForVBlank();
    return cpuGetTiming();
}

static void publish(void)
{
    obs.magic=NDS_RENDER_PROBE_MAGIC;obs.abi_version=NDS_RENDER_ABI_VERSION;
    obs.size_bytes=sizeof(obs);obs.sequence=g_nds_render_probe.sequence+1u;
    g_nds_render_probe.sequence=obs.sequence;
    DC_FlushRange((const void *)&g_nds_render_probe.sequence,32);
    obs.vblank_count=vblank_counter;
    memcpy((void *)&g_nds_render_probe,&obs,sizeof(obs));
    DC_FlushRange((const void *)&g_nds_render_probe,sizeof(obs));
    sprite_obs.magic=0x12504b44u;sprite_obs.abi_version=1;sprite_obs.size_bytes=sizeof(sprite_obs);
    sprite_obs.sequence=g_nds_sprite_probe.sequence+1u;
    g_nds_sprite_probe.sequence=sprite_obs.sequence;
    DC_FlushRange((const void *)&g_nds_sprite_probe.sequence,32);
    memcpy((void *)&g_nds_sprite_probe,&sprite_obs,sizeof(sprite_obs));
    DC_FlushRange((const void *)&g_nds_sprite_probe,sizeof(sprite_obs));
    ++g_nds_sprite_probe.sequence;
    DC_FlushRange((const void *)&g_nds_sprite_probe.sequence,32);
    ++g_nds_render_probe.sequence;
    DC_FlushRange((const void *)&g_nds_render_probe.sequence,32);
}

static int memory_setup(void)
{
    void *allocation;
    uint32_t cursor;
    obs.region_base=DOK_NDS_MAIN_BASE;obs.region_limit=DOK_NDS_ARM9_LIMIT;
    obs.static_end=address(&__end__);obs.heap_start=address(getHeapStart());
    obs.heap_limit_before=address(getHeapLimit());obs.is_dsi=isDSiMode();
    obs.reserve_bytes=DOK_NDS_RESERVE_BYTES;obs.sdk_heap_min=DOK_NDS_SDK_HEAP_MIN;
    if(obs.is_dsi || address(&__eheap_end)!=DOK_NDS_ARM9_LIMIT ||
       obs.heap_limit_before<DOK_NDS_ARM9_LIMIT ||
       obs.static_end>DOK_NDS_MAIN_BASE+DOK_NDS_STATIC_CEILING) do{if(!g_nds_effect_probe[31])g_nds_effect_probe[31]=__LINE__;DC_FlushRange((const void *)g_nds_effect_probe,128);return 0;}while(0);
    if(obs.heap_limit_before>DOK_NDS_ARM9_LIMIT &&
       reduceHeapSize(obs.heap_limit_before-DOK_NDS_ARM9_LIMIT)!=0) do{if(!g_nds_effect_probe[31])g_nds_effect_probe[31]=__LINE__;DC_FlushRange((const void *)g_nds_effect_probe,128);return 0;}while(0);
    obs.heap_limit_policy=address(getHeapLimit());
    if(reduceHeapSize(DOK_NDS_RESERVE_BYTES)!=0) do{if(!g_nds_effect_probe[31])g_nds_effect_probe[31]=__LINE__;DC_FlushRange((const void *)g_nds_effect_probe,128);return 0;}while(0);
    obs.malloc_request_bytes=DOK_NDS_SLAB_BYTES+31u;
    allocation=malloc(obs.malloc_request_bytes);
    if(!allocation) do{if(!g_nds_effect_probe[31])g_nds_effect_probe[31]=__LINE__;DC_FlushRange((const void *)g_nds_effect_probe,128);return 0;}while(0);
    managed=(unsigned char *)(((uintptr_t)allocation+31u)&~(uintptr_t)31u);
    obs.managed_base=address(managed);obs.managed_bytes=DOK_NDS_SLAB_BYTES;
    cursor=0;storage=(SpriteWorld *)(void *)(managed+cursor);world=&storage->geometry;cursor+=DOK_NDS_WORLD_BYTES;
    events=(dok_event_bank *)(void *)(managed+cursor);cursor+=DOK_NDS_EVENTS_BYTES;
    storage->effects.trace_total=0;
    scratch=(dok_render_workspace *)(void *)(managed+cursor);cursor+=DOK_NDS_SCRATCH_BYTES;
    scene=(dok_render_scene *)(void *)(managed+cursor);cursor+=DOK_NDS_SCENE_BYTES;
    assets=managed+cursor;cursor+=DOK_NDS_ASSETS_BYTES;
    cursor+=DOK_NDS_AUDIO_BYTES;
    io=(ProbeIO *)(void *)(managed+cursor);cursor+=DOK_NDS_IO_BYTES;
    if(cursor!=DOK_NDS_SLAB_BYTES) do{if(!g_nds_effect_probe[31])g_nds_effect_probe[31]=__LINE__;DC_FlushRange((const void *)g_nds_effect_probe,128);return 0;}while(0);
    obs.world_used=obs.world_high_water=sizeof(*storage);
    obs.scratch_used=obs.scratch_high_water=sizeof(*scratch);
    obs.scene_used=obs.scene_high_water=sizeof(*scene);
    return 1;
}

static int memory_check(void)
{
    uint32_t heap=address(getHeapEnd());
    obs.heap_end=heap;obs.heap_limit_final=address(getHeapLimit());
    if(heap<obs.static_end+DOK_NDS_SLAB_BYTES) do{if(!g_nds_effect_probe[31])g_nds_effect_probe[31]=__LINE__;DC_FlushRange((const void *)g_nds_effect_probe,128);return 0;}while(0);
    obs.static_startup_bytes=heap-DOK_NDS_MAIN_BASE-DOK_NDS_SLAB_BYTES;
    obs.sdk_heap_available=obs.heap_limit_final-heap;
    return obs.heap_limit_final==DOK_NDS_ARM9_LIMIT-DOK_NDS_RESERVE_BYTES &&
        obs.static_startup_bytes<=DOK_NDS_STATIC_CEILING &&
        obs.sdk_heap_available>=DOK_NDS_SDK_HEAP_MIN;
}

static int position(uint32_t case_id, const dok_nds_render_camera *camera,
                    int32_t ox, int32_t oy, int32_t oz,
                    int32_t wx, int32_t wy, int32_t wz)
{
    NdsRenderPositionRecord row = {0};
    dok_nds_render_cell cell = {ox, oy, oz, NULL, 0};
    dok_nds_render_vertex vertex = {wx-ox, wy-oy, wz-oz, 0, 0};
    dok_nds_render_clip clip;
    int32_t model[12], combined[16];
    uint32_t index = obs.position_count;
    if (index >= NDS_RENDER_POSITION_CAPACITY ||
        dok_nds_render_position_test(camera, &cell, &vertex, &clip) != DOK_NDS_RENDER_OK ||
        dok_nds_render_cell_matrix(camera, &cell, model) != DOK_NDS_RENDER_OK ||
        dok_nds_render_clip_matrix(combined) != DOK_NDS_RENDER_OK) do{if(!g_nds_effect_probe[31])g_nds_effect_probe[31]=__LINE__;DC_FlushRange((const void *)g_nds_effect_probe,128);return 0;}while(0);
    row.magic=NDS_RENDER_POSITION_MAGIC; row.abi_version=NDS_RENDER_ABI_VERSION;
    row.size_bytes=sizeof(row); row.sequence=1; row.ordinal=index+1; row.case_id=case_id;
    row.origin_x=(uint32_t)ox; row.origin_y=(uint32_t)oy; row.origin_z=(uint32_t)oz;
    row.local_x=(uint32_t)vertex.x; row.local_y=(uint32_t)vertex.y; row.local_z=(uint32_t)vertex.z;
    row.camera_x=(uint32_t)camera->x; row.camera_y=(uint32_t)camera->y;
    row.camera_z=(uint32_t)camera->z; row.sin_q15=(uint32_t)camera->sin_q15;
    row.cos_q15=(uint32_t)camera->cos_q15;
    row.clip_x=(uint32_t)clip.x; row.clip_y=(uint32_t)clip.y;
    row.clip_z=(uint32_t)clip.z; row.clip_w=(uint32_t)clip.w;
    memcpy(&row.modelview_0, model, sizeof(model));
    memcpy(&row.clipmatrix_0, combined, sizeof(combined));
    g_nds_render_positions[index].sequence=row.sequence;
    DC_FlushRange((const void *)&g_nds_render_positions[index].sequence,32);
    memcpy((void *)&g_nds_render_positions[index], &row, sizeof(row));
    DC_FlushRange((const void *)&g_nds_render_positions[index], sizeof(row));
    ++g_nds_render_positions[index].sequence;
    DC_FlushRange((const void *)&g_nds_render_positions[index].sequence, 32);
    ++obs.position_count;
    return 1;
}


static int fixture_load(void)
{
    FILE *file=fopen("fat:/dok/scene.dat","rb");
    unsigned char digest[32];dok_sha256 hash;
    uint32_t i,expected;
    if(!file) do{if(!g_nds_effect_probe[31])g_nds_effect_probe[31]=__LINE__;DC_FlushRange((const void *)g_nds_effect_probe,128);return 0;}while(0);
    fixture_bytes=(uint32_t)fread(io->fixture,1,sizeof(io->fixture),file);
    if(fgetc(file)!=EOF || ferror(file)) {fclose(file);do{if(!g_nds_effect_probe[31])g_nds_effect_probe[31]=__LINE__;DC_FlushRange((const void *)g_nds_effect_probe,128);return 0;}while(0);}
    fclose(file);
    if(fixture_bytes<DOK_RENDER_FIXTURE_HEADER ||
       memcmp(io->fixture,"N07SCN1\0",8) || le32(io->fixture+8)!=1 ||
       le32(io->fixture+16)!=64 || le32(io->fixture+20)!=10240 ||
       le32(io->fixture+28)!=0) do{if(!g_nds_effect_probe[31])g_nds_effect_probe[31]=__LINE__;DC_FlushRange((const void *)g_nds_effect_probe,128);return 0;}while(0);
    fixture_count=le32(io->fixture+12);
    expected=64u+fixture_count*64u+3u*10240u;
    if(!fixture_count || fixture_count>64u || fixture_bytes!=expected ||
       le32(io->fixture+24)!=fixture_bytes) do{if(!g_nds_effect_probe[31])g_nds_effect_probe[31]=__LINE__;DC_FlushRange((const void *)g_nds_effect_probe,128);return 0;}while(0);
    dok_sha256_begin(&hash);dok_sha256_update(&hash,io->fixture+64,fixture_bytes-64);
    dok_sha256_end(&hash,digest);
    if(memcmp(digest,io->fixture+32,32)) do{if(!g_nds_effect_probe[31])g_nds_effect_probe[31]=__LINE__;DC_FlushRange((const void *)g_nds_effect_probe,128);return 0;}while(0);
    for(i=0;i<fixture_count;++i) {
        const unsigned char *p=io->fixture+64+i*64;
        dok_render_case *c=&cases[i];uint32_t j;
        c->id=le32(p);c->level=le32(p+4);c->x=signed_word(le32(p+8));
        c->y=signed_word(le32(p+12));c->z=signed_word(le32(p+16));
        c->heading=le32(p+20);c->dx=signed_word(le32(p+24));
        c->dz=signed_word(le32(p+28));c->dheading=signed_word(le32(p+32));
        c->frames=le32(p+36);c->flags=le32(p+40);c->source_kind=le32(p+44);
        c->cell_x=le32(p+48);c->cell_z=le32(p+52);
        c->reserved0=le32(p+56);c->reserved1=le32(p+60);
        if(!c->id || c->level<1 || c->level>3 || c->heading>360 ||
           !c->frames || c->frames>1024 || c->flags>7 || c->reserved0 || c->reserved1)
            do{if(!g_nds_effect_probe[31])g_nds_effect_probe[31]=__LINE__;DC_FlushRange((const void *)g_nds_effect_probe,128);return 0;}while(0);
        for(j=0;j<i;++j) if(cases[j].id==c->id) do{if(!g_nds_effect_probe[31])g_nds_effect_probe[31]=__LINE__;DC_FlushRange((const void *)g_nds_effect_probe,128);return 0;}while(0);
    }
    obs.case_count=fixture_count;obs.source_packet_address=address(io->fixture);
    obs.source_packet_bytes=fixture_bytes;
    DC_FlushRange(io->fixture,fixture_bytes);
    return 1;
}

static const unsigned char *level_block(uint32_t level)
{ return io->fixture+64u+fixture_count*64u+(level-1u)*10240u; }

static void draw_hud(uint32_t level)
{
    const unsigned char *block=level_block(level);
    DC_FlushRange(block,10240);
    dmaCopy(block,BG_PALETTE,512);
    dmaFillHalfWords(0,bgGetGfxPtr(hud_bg),65536);
    dmaCopy(block+512,(unsigned char *)bgGetGfxPtr(hud_bg)+154u*256u,9728);
}

static void diagnostics(const dok_nds_render_stats *stats)
{
    consoleSelect(&bottom);consoleClear();
    printf("N13 DS effects / LEVEL %lu\ncase %lu/%lu frame %lu\n\n",
        (unsigned long)obs.level,(unsigned long)obs.case_id,
        (unsigned long)obs.case_count,(unsigned long)obs.frame_id);
    printf("quads %lu / hardware %lu\nvertices %lu / 6144\n",
        (unsigned long)stats->submitted_quads,(unsigned long)stats->hardware_polygons,
        (unsigned long)stats->hardware_vertices);
    printf("textures %lu / 393216\npalettes %lu / 16384\n",
        (unsigned long)stats->texture_bytes,(unsigned long)stats->palette_bytes);
    printf("flags %04lX / %04lX\nRAM reserve %lu\n\nA: next case\n%s\nPhysical DS: N24\n",
        (unsigned long)obs.disp3d_sticky,(unsigned long)obs.gxstat_sticky,
        (unsigned long)obs.reserve_bytes,obs.flat_diagnostic?"FLAT COVERAGE":"ORIGINAL TEXTURES");
}

static void history(NdsRenderHistoryRecord *record)
{
    uint32_t index=obs.history_total%NDS_RENDER_HISTORY_CAPACITY;
    record->magic=NDS_RENDER_HISTORY_MAGIC;record->abi_version=NDS_RENDER_ABI_VERSION;
    record->size_bytes=sizeof(*record);record->sequence=g_nds_render_history[index].sequence+1u;
    record->ordinal=++obs.history_total;
    g_nds_render_history[index].sequence=record->sequence;
    DC_FlushRange((const void *)&g_nds_render_history[index].sequence,32);
    memcpy((void *)&g_nds_render_history[index],record,sizeof(*record));
    DC_FlushRange((const void *)&g_nds_render_history[index],sizeof(*record));
    ++g_nds_render_history[index].sequence;
    DC_FlushRange((const void *)&g_nds_render_history[index].sequence,32);
}

static void observe(dok_nds_render_stats *stats)
{
    dok_nds_render_observe(stats);
    obs.disp3d_sticky|=stats->disp3d_flags;obs.gxstat_sticky|=stats->gxstat_flags;
    if(!obs.rdlines_samples || stats->rdlines<obs.rdlines_min) obs.rdlines_min=stats->rdlines;
    if(stats->rdlines>obs.rdlines_max) obs.rdlines_max=stats->rdlines;
    ++obs.rdlines_samples;
}

static int level_load(uint32_t level)
{
    NdsRenderHistoryRecord row={0};
    mask_identity_count=0;
#if EFFECT_LIGHT_CACHE
    dok_light_cache_reset(&geometry_cache);
#endif
    uint32_t start=cpuGetTiming(),i;
    row.flags=NDS_RENDER_FRAME_LOADING;row.case_id=obs.case_id;row.level=level;
    obs.flags=NDS_RENDER_FRAME_LOADING;
    row.vblank_start=vblank_counter;
    if(dok_render_world_load(&session,level,level,world,scratch,assets,
        DOK_NDS_ASSETS_BYTES)!=DOK_STATUS_OK) do{if(!g_nds_effect_probe[31])g_nds_effect_probe[31]=__LINE__;DC_FlushRange((const void *)g_nds_effect_probe,128);return 0;}while(0);
    if(SPRITE_LANGUAGE==2) {
        uint32_t off=(world->asset_bytes+31u)&~31u;dok_asset_view view;
        dok_asset_id id=UINT32_C(2)<<30|(17u+level)<<18|(uint32_t)DOK_ASSET_LEVEL_CP866<<10;
        if(dok_assets_load(&session,id,assets+off,DOK_RENDER_SOURCE_BYTES,&view)!=DOK_ASSET_OK ||
           dok_level_parse(&view,&scratch->world.level,&world->base)!=DOK_WORLD_OK ||
           dok_presentation_bind(&world->effective,&world->base,&session,&scratch->presentation,&world->presentation)!=DOK_BALANCE_OK)
            do{if(!g_nds_effect_probe[31])g_nds_effect_probe[31]=__LINE__;DC_FlushRange((const void *)g_nds_effect_probe,128);return 0;}while(0);
        world->asset_bytes=off+view.entry.bytes;
    }
    i=(world->asset_bytes+31u)&~31u;
    if(dok_sprite_assets_load(&world->presentation,&storage->bank,assets+i,DOK_NDS_ASSETS_BYTES-i)!=DOK_STATUS_OK)
        do{if(!g_nds_effect_probe[31])g_nds_effect_probe[31]=__LINE__;DC_FlushRange((const void *)g_nds_effect_probe,128);return 0;}while(0);
    i=(i+storage->bank.metadata_bytes+31u)&~31u;
    if(i+EFFECT_POOL_BYTES>=DOK_NDS_ASSETS_BYTES)do{if(!g_nds_effect_probe[31])g_nds_effect_probe[31]=__LINE__;DC_FlushRange((const void *)g_nds_effect_probe,128);return 0;}while(0);
    dok_texture_cache_init(&storage->cache,assets+i,DOK_NDS_ASSETS_BYTES-i-EFFECT_POOL_BYTES);
    /* Measured N12 cold turns stalled on FAT/catalog/hash reads. Initial
     * bindings are a bounded level working set: verify them at the explicit
     * load boundary, while later animation frames remain demand-loaded. */
    if(dok_texture_cache_begin(&storage->cache)!=DOK_STATUS_OK)do{if(!g_nds_effect_probe[31])g_nds_effect_probe[31]=__LINE__;DC_FlushRange((const void *)g_nds_effect_probe,128);return 0;}while(0);
    for(uint32_t glyph=0;glyph<256u;++glyph)if(storage->bank.bindings[glyph].frame.image) {
        const dok_asset_entry *e=dok_sprite_asset_image(&storage->bank,storage->bank.bindings[glyph].frame.image);
        if(!e || dok_texture_cache_require(&storage->cache,&session,e)!=DOK_STATUS_OK)do{if(!g_nds_effect_probe[31])g_nds_effect_probe[31]=__LINE__;DC_FlushRange((const void *)g_nds_effect_probe,128);return 0;}while(0);
    }
    if(nds_effects_load(&storage->effects,world,&storage->bank,&session,&reader,events,&platform,
        assets+DOK_NDS_ASSETS_BYTES-EFFECT_POOL_BYTES,EFFECT_POOL_BYTES)!=DOK_STATUS_OK)do{if(!g_nds_effect_probe[31])g_nds_effect_probe[31]=__LINE__;DC_FlushRange((const void *)g_nds_effect_probe,128);return 0;}while(0);
#if EFFECT_GPU_FOG
    /* Profiled turn stalls were2KiB source animation reads, not GPU shading.
     * Verify the bounded active animation working set at level loading along
     * with initial bindings. Reuse RAM released by the LIGHT variant cache. */
    for(uint32_t glyph=0;glyph<256u;++glyph) {
        const dok_sprite_animation *animation=&storage->effects.players[glyph].animation;
        for(uint32_t f=0;f<animation->count;++f) {
            dok_sprite_frame value;
            if(dok_sprite_animation_get(animation,(int32_t)f,&value)!=DOK_STATUS_OK) return 0;
            const dok_asset_entry *e=dok_sprite_asset_image(&storage->bank,value.image);
            if(!e || dok_texture_cache_require(&storage->cache,&session,e)!=DOK_STATUS_OK)return 0;
        }
    }
#endif
    if(dok_nds_stream_reset(storage->effects.palette,4096)!=DOK_NDS_RENDER_OK)do{if(!g_nds_effect_probe[31])g_nds_effect_probe[31]=__LINE__;DC_FlushRange((const void *)g_nds_effect_probe,128);return 0;}while(0);
    stream_ready=0;
    draw_hud(level);
    obs.assets_used=i;obs.assets_high_water=i;
    sprite_obs.list_address=address(&storage->list);sprite_obs.list_bytes=sizeof(storage->list);
    sprite_obs.bank_address=address(&storage->bank);sprite_obs.bank_bytes=sizeof(storage->bank);
    sprite_obs.binding_bytes=sizeof(dok_sprite_binding);sprite_obs.language=SPRITE_LANGUAGE;
    sprite_obs.image_count=storage->bank.image_count;sprite_obs.animation_count=storage->bank.animation_count;
    sprite_obs.demand_address=address(demand);sprite_obs.demand_bytes=sizeof(dok_nds_stream_texture);
    hash_bytes(&world->initial,sizeof(world->initial),world_digest);
    row.io_upload_ticks=cpuGetTiming()-start;row.total_ticks=row.io_upload_ticks;
    row.vblank_end=vblank_counter;
    history(&row);
    return memory_check();
}

static void begin_presentation_sequence(const dok_render_case *c)
{
    NdsRenderHistoryRecord row={0};
    uint32_t start=cpuGetTiming();
    row.flags=NDS_RENDER_FRAME_SYNC;row.case_id=c->id;row.level=c->level;
    row.frame_id=obs.frame_id;row.presented_frame_id=obs.presented_frame_id;
    row.vblank_start=vblank_counter;
    /* Loading/READY ends at an arbitrary scan phase. Begin the active
     * sequence on a fresh presentation boundary, without submitting a frame.
     * Anchor before telemetry: its publication belongs to the first frame. */
    previous_present_ticks=wait_presentation_boundary();previous_present_vblank=vblank_counter;
    row.vblank_end=previous_present_vblank;row.total_ticks=previous_present_ticks-start;
    obs.flags=row.flags;history(&row);publish();
}

static int sprites_prepare(const dok_render_case *c,const dok_nds_render_camera *camera,uint32_t heading,uint32_t ordinal)
{
    const dok_sprite_list *list;
    dok_sprite_snapshot snapshot={world->initial.epoch,scene->generation,storage->bank.bindings,&world->initial};
    uint32_t i,j,base_demands;
    uint16_t material_map[DOK_RENDER_MATERIALS];
    if(dok_sprites_gather(&snapshot,camera,heading,&storage->list,&list)!=DOK_STATUS_OK)do{if(!g_nds_effect_probe[31])g_nds_effect_probe[31]=__LINE__;DC_FlushRange((const void *)g_nds_effect_probe,128);return 0;}while(0);
    sprite_obs.frame_fixture=0;
    if(c->source_kind==13u || c->source_kind==17u) {
        dok_sprite_frame f;
        if(camera->x || camera->z || heading ||
           dok_sprite_animation_get(&storage->bank.sequences[4],0,&f)!=DOK_STATUS_OK)do{if(!g_nds_effect_probe[31])g_nds_effect_probe[31]=__LINE__;DC_FlushRange((const void *)g_nds_effect_probe,128);return 0;}while(0);
        scene->counts.published_quads=2;scene->frame.cell_count=2;
        for(i=0;i<2u;++i) {
            dok_nds_render_quad *q=&scene->quads[i];memset(q,0,sizeof(*q));
            q->texture_slot=UINT16_MAX;q->color_rgb555=i?31u:31744u;q->source_surface_id=i+1u;
            int32_t left=i?0:-2000,right=2000,depth=i?900:1800;
            q->vertex[0]=(dok_nds_render_vertex){left,-1200,depth,0,0};
            q->vertex[1]=(dok_nds_render_vertex){right,-1200,depth,0,0};
            q->vertex[2]=(dok_nds_render_vertex){right,0,depth,0,0};
            q->vertex[3]=(dok_nds_render_vertex){left,0,depth,0,0};
            scene->cells[i]=(dok_nds_render_cell){0,0,0,q,1};
        }
        storage->list.count=2;
        for(i=0;i<2u;++i) {
            dok_sprite_pose p={0};p.x=i?-80:-360;p.z=i?700:1300;p.y=0;
            p.width=(int16_t)(f.width*(i?5:8));p.height=(int16_t)(f.height*(i?5:8));
            p.texture_width=f.width;p.texture_height=f.height;p.image=f.image;p.glyph=256;
            p.source_id=0xc1000000u|i;storage->list.poses[i]=p;
        }
    }
    if(c->source_kind==12u) {
        uint32_t frame=ordinal,seq;dok_sprite_frame f;
        for(seq=0;seq<storage->bank.animation_count;++seq) {
            if(frame<storage->bank.sequences[seq].count)break;
            frame-=storage->bank.sequences[seq].count;
        }
        if(seq<storage->bank.animation_count) {
            dok_sprite_pose p={0};
            if(dok_sprite_animation_get(&storage->bank.sequences[seq],(int32_t)frame,&f)!=DOK_STATUS_OK ||
               storage->list.count==DOK_SPRITE_LIMIT)do{if(!g_nds_effect_probe[31])g_nds_effect_probe[31]=__LINE__;DC_FlushRange((const void *)g_nds_effect_probe,128);return 0;}while(0);
            p.x=camera->x-(camera->sin_q15*720)/32768;
            p.z=camera->z+(camera->cos_q15*720)/32768;p.y=-40;
            p.width=(int16_t)(f.width*3);p.height=(int16_t)(f.height*3);
            p.texture_width=f.width;p.texture_height=f.height;p.image=f.image;p.glyph=256;
            p.source_id=0xc0000000u|ordinal;
            storage->list.poses[storage->list.count++]=p;sprite_obs.frame_fixture=f.image;
        }
    }
    demand_count=0;
    if(dok_texture_cache_begin(&storage->cache)!=DOK_STATUS_OK)do{if(!g_nds_effect_probe[31])g_nds_effect_probe[31]=__LINE__;DC_FlushRange((const void *)g_nds_effect_probe,128);return 0;}while(0);
    for(i=0;i<DOK_RENDER_MATERIALS;++i)material_map[i]=UINT16_MAX;
    for(i=0;i<scene->counts.published_quads;++i) {
        dok_nds_render_quad *q=&scene->quads[i];uint32_t old=q->texture_slot;
        if(old==UINT16_MAX)continue;
        if(old>=world->material_count)do{if(!g_nds_effect_probe[31])g_nds_effect_probe[31]=__LINE__;DC_FlushRange((const void *)g_nds_effect_probe,128);return 0;}while(0);
        if(material_map[old]==UINT16_MAX) {
            const dok_render_material *m=&world->materials[old];
            if(demand_count==DOK_NDS_RENDER_TEXTURE_CAPACITY)do{if(!g_nds_effect_probe[31])g_nds_effect_probe[31]=__LINE__;DC_FlushRange((const void *)g_nds_effect_probe,128);return 0;}while(0);
            material_map[old]=(uint16_t)demand_count;
            raw_material[demand_count]=(uint16_t)old;
            demand[demand_count++]=(dok_nds_stream_texture){((uint64_t)m->color0_transparent<<32)|m->entry.id,
                {m->entry.id,m->pixels,m->entry.bytes,NULL,m->entry.storage_width,m->entry.storage_height,
                 m->entry.width,m->entry.height,m->color0_transparent}};
        }
        q->texture_slot=material_map[old];
    }
    base_demands=demand_count;
    for(i=0;i<storage->list.count;++i) {
        const dok_sprite_pose *p=&storage->list.poses[i];
        const dok_asset_entry *e=dok_sprite_asset_image(&storage->bank,p->image);
        uint32_t transparent=p->glyph==256u?1u:(storage->bank.bindings[p->glyph].flags&8u)!=0;
        uint64_t key;
        if(!e)do{if(!g_nds_effect_probe[31])g_nds_effect_probe[31]=__LINE__;DC_FlushRange((const void *)g_nds_effect_probe,128);return 0;}while(0);
        key=((uint64_t)transparent<<32)|e->id;
        for(j=0;j<demand_count;++j)if(demand[j].key==key)break;
        if(j==demand_count) {
            if(j==DOK_NDS_RENDER_TEXTURE_CAPACITY ||
               dok_texture_cache_require(&storage->cache,&session,e)!=DOK_STATUS_OK)do{if(!g_nds_effect_probe[31])g_nds_effect_probe[31]=__LINE__;DC_FlushRange((const void *)g_nds_effect_probe,128);return 0;}while(0);
            raw_material[j]=UINT16_MAX;
            demand[j]=(dok_nds_stream_texture){key,{e->id,NULL,e->bytes,NULL,e->storage_width,e->storage_height,
                e->width,e->height,transparent}};++demand_count;
        }
        sprite_slots[i]=(uint16_t)j;
    }
    for(i=base_demands;i<demand_count;++i) {
        demand[i].texture.pixels=dok_texture_cache_pixels(&storage->cache,demand[i].texture.asset_id);
        if(!demand[i].texture.pixels)do{if(!g_nds_effect_probe[31])g_nds_effect_probe[31]=__LINE__;DC_FlushRange((const void *)g_nds_effect_probe,128);return 0;}while(0);
    }
    if(dok_sprites_append(scene,&storage->list,sprite_slots,scene->generation)!=DOK_RENDER_SCENE_OK)do{if(!g_nds_effect_probe[31])g_nds_effect_probe[31]=__LINE__;DC_FlushRange((const void *)g_nds_effect_probe,128);return 0;}while(0);

    sprite_obs.sprite_count=storage->list.count;sprite_obs.considered=storage->list.considered;
    sprite_obs.rejected=storage->list.rejected;sprite_obs.demand_count=demand_count;
    sprite_obs.cache_bytes=storage->cache.used;sprite_obs.cpu_hits=storage->cache.hits;
    sprite_obs.cpu_misses=storage->cache.misses;sprite_obs.cpu_evictions=storage->cache.evictions;
    sprite_obs.cpu_reads=storage->cache.read_bytes;sprite_obs.gpu_hits=stream_stats.hits;
    sprite_obs.gpu_misses=stream_stats.misses;sprite_obs.gpu_evictions=stream_stats.evictions;
    sprite_obs.gpu_rebuilds=stream_stats.rebuilds;
    obs.texture_count=demand_count;obs.texture_bytes=stream_stats.resident_bytes;
    if(obs.assets_used+storage->cache.used>obs.assets_high_water)obs.assets_high_water=obs.assets_used+storage->cache.used;
    if(SPRITE_AUDIT) {
        unsigned char hash[32];hash_bytes(&world->initial,sizeof(world->initial),hash);
        if(memcmp(hash,world_digest,32))do{if(!g_nds_effect_probe[31])g_nds_effect_probe[31]=__LINE__;DC_FlushRange((const void *)g_nds_effect_probe,128);return 0;}while(0);
    }
    return 1;
}

#include "effects_prepare.inc"

static int render_one(const dok_render_case *c,uint32_t ordinal)
{
    dok_nds_render_camera camera;
    const dok_nds_render_frame *frame=NULL;
    dok_nds_render_stats stats={0};NdsRenderHistoryRecord row={0};
    uint32_t start=cpuGetTiming(),phase=start,i,present_ticks;
    int64_t x=(int64_t)c->x+(int64_t)c->dx*ordinal;
    int64_t z=(int64_t)c->z+(int64_t)c->dz*ordinal;
    int64_t heading=((int64_t)c->heading+(int64_t)c->dheading*ordinal)%360;
    if(x<INT32_MIN || x>INT32_MAX || z<INT32_MIN || z>INT32_MAX) do{if(!g_nds_effect_probe[31])g_nds_effect_probe[31]=__LINE__;DC_FlushRange((const void *)g_nds_effect_probe,128);return 0;}while(0);
    if(heading<0) heading+=360;
    camera=(dok_nds_render_camera){(int32_t)x,c->y,(int32_t)z,0,0};
    if(!dok_render_trig((uint32_t)heading,&camera.sin_q15,&camera.cos_q15)) do{if(!g_nds_effect_probe[31])g_nds_effect_probe[31]=__LINE__;DC_FlushRange((const void *)g_nds_effect_probe,128);return 0;}while(0);
    row.vblank_start=previous_present_ticks?previous_present_vblank:vblank_counter;
    row.case_id=c->id;row.level=c->level;
    row.frame_id=++obs.frame_id;row.flags=(c->flags&DOK_RENDER_CASE_MOVING)?NDS_RENDER_FRAME_MOVING:0;
    if(c->flags&DOK_RENDER_CASE_WARMUP) row.flags|=NDS_RENDER_FRAME_WARMUP;
    if(scene->leased && dok_render_scene_release(scene,scene->generation)!=DOK_RENDER_SCENE_OK)
        do{if(!g_nds_effect_probe[31])g_nds_effect_probe[31]=__LINE__;DC_FlushRange((const void *)g_nds_effect_probe,128);return 0;}while(0);
    storage->effects.case_id=c->id;storage->effects.frame_id=obs.frame_id;
    if(nds_effects_begin(&storage->effects)!=DOK_STATUS_OK)do{if(!g_nds_effect_probe[31])g_nds_effect_probe[31]=__LINE__;DC_FlushRange((const void *)g_nds_effect_probe,128);return 0;}while(0);
    uint32_t profile[8]={c->id,obs.frame_id,0,0,0,0,0,0};
    dok_interaction_phase_view phase_view={0};dok_door_render_counts door_counts;
    if(c->source_kind==15u) {
        phase_view.world_epoch=world->initial.epoch;phase_view.revision=obs.frame_id;
        uint32_t p=ordinal%60u;p=p<30u?p:59u-p;
        for(i=0;i<world->effective.object_count;++i)if(world->effective.objects[i].class_id==7u) {
            uint32_t glyph=world->effective.objects[i].glyph;phase_view.phase[glyph]=(int16_t)p;phase_view.extent[glyph]=30;
        }
    }
    if(dok_door_scene_prepare(scene,&world->initial,world->bindings,&camera,
        (uint32_t)heading,c->source_kind==15u?&phase_view:NULL,&door_counts,&frame)!=DOK_RENDER_SCENE_OK) do{if(!g_nds_effect_probe[31])g_nds_effect_probe[31]=__LINE__;DC_FlushRange((const void *)g_nds_effect_probe,128);return 0;}while(0);
    profile[2]=cpuGetTiming()-phase;
    if(!sprites_prepare(c,&camera,(uint32_t)heading,ordinal))do{if(!g_nds_effect_probe[31])g_nds_effect_probe[31]=__LINE__;DC_FlushRange((const void *)g_nds_effect_probe,128);return 0;}while(0);
    profile[3]=cpuGetTiming()-phase-profile[2];
    if(c->source_kind==12u && sprite_obs.frame_fixture) {
        NdsSpriteTrace trace={0};unsigned char hash[32];
        const dok_nds_render_texture *t=&demand[sprite_slots[storage->list.count-1u]].texture;
        if(sprite_obs.trace_count==NDS_SPRITE_TRACE_CAPACITY)do{if(!g_nds_effect_probe[31])g_nds_effect_probe[31]=__LINE__;DC_FlushRange((const void *)g_nds_effect_probe,128);return 0;}while(0);
        trace.image_id=t->asset_id;trace.frame_id=obs.frame_id;trace.case_id=c->id;
        trace.width=t->logical_width;trace.height=t->logical_height;trace.bytes=t->bytes;
        hash_bytes(t->pixels,t->bytes,hash);memcpy(&trace.hash0,hash,32);
        memcpy((void *)&g_nds_sprite_trace[sprite_obs.trace_count],&trace,sizeof(trace));
        DC_FlushRange((const void *)&g_nds_sprite_trace[sprite_obs.trace_count],sizeof(trace));
        ++sprite_obs.trace_count;
    }
    if(!effects_prepare(c,ordinal))do{if(!g_nds_effect_probe[31])g_nds_effect_probe[31]=__LINE__;DC_FlushRange((const void *)g_nds_effect_probe,128);return 0;}while(0);
    profile[4]=cpuGetTiming()-phase-profile[2]-profile[3];
    obs.flat_diagnostic=(c->flags&DOK_RENDER_CASE_FLAT)?1u:0u;
    if(obs.flat_diagnostic) row.flags|=NDS_RENDER_FRAME_FLAT_DIAGNOSTIC;
    if(obs.flat_diagnostic) for(i=0;i<scene->counts.published_quads;++i) {
        dok_nds_render_quad *q=&scene->quads[i];
        uint32_t color=(q->source_surface_id*UINT32_C(2654435761))>>17;
        q->texture_slot=UINT16_MAX;q->reserved=0;q->color_rgb555=(uint16_t)(color?color:1u);
    }
    row.prepare_ticks=cpuGetTiming()-phase;
    profile[5]=row.prepare_ticks;profile[6]=sprite_obs.cpu_reads;profile[7]=storage->effects.cache.generated_bytes;
    if(c->id<64u && row.prepare_ticks>g_nds_effect_profile[c->id][5]) {
        memcpy((void *)g_nds_effect_profile[c->id],profile,sizeof(profile));
        DC_FlushRange((const void *)g_nds_effect_profile[c->id],sizeof(profile));
    }
    phase=cpuGetTiming();
    uint32_t cold=!stream_ready;
    if(cold) {
        row.flags|=NDS_RENDER_FRAME_LOADING;
        if(dok_nds_stream_commit(&stream_stats)!=DOK_NDS_RENDER_OK)do{if(!g_nds_effect_probe[31])g_nds_effect_probe[31]=__LINE__;DC_FlushRange((const void *)g_nds_effect_probe,128);return 0;}while(0);
        row.io_upload_ticks=cpuGetTiming()-phase;phase=cpuGetTiming();stream_ready=1;
    }
    if(dok_nds_render_submit(frame,&stats)!=DOK_NDS_RENDER_OK) do{if(!g_nds_effect_probe[31])g_nds_effect_probe[31]=__LINE__;DC_FlushRange((const void *)g_nds_effect_probe,128);return 0;}while(0);
    row.geometry_wait_ticks=stats.geometry_wait_ticks;
    row.submit_ticks=cpuGetTiming()-phase-row.geometry_wait_ticks;
    obs.submitted_frame_id=obs.frame_id;
    row.input_quads=stats.input_quads;row.submitted_quads=stats.submitted_quads;
    row.submitted_vertices=stats.submitted_vertices;row.hardware_polygons=stats.hardware_polygons;
    row.hardware_vertices=stats.hardware_vertices;row.texture_bytes=stats.texture_bytes;
    row.palette_bytes=stats.palette_bytes;
    /* The interval starts at the previous presentation boundary, so compact
     * history/probe publishing after it is included in the following frame.
     * Full diagnostic text and snapshot flush have their own explicit phase. */
    present_ticks=wait_presentation_boundary();
    phase=cpuGetTiming();
    if(!cold) {
        dok_nds_render_result result=dok_nds_stream_commit(&stream_stats);
        sprite_obs.commit_start_line=stream_stats.commit_start_line;sprite_obs.commit_end_line=stream_stats.commit_end_line;
        if(result!=DOK_NDS_RENDER_OK)do{if(!g_nds_effect_probe[31])g_nds_effect_probe[31]=__LINE__;DC_FlushRange((const void *)g_nds_effect_probe,128);return 0;}while(0);
    }
    if(!cold)row.io_upload_ticks=cpuGetTiming()-phase;
    row.uploads=stream_stats.uploads;row.upload_bytes=stream_stats.upload_bytes;
    obs.uploads_total+=row.uploads;obs.upload_bytes_total+=row.upload_bytes;
    obs.cache_misses+=stream_stats.misses;obs.cache_evictions+=stream_stats.evictions;
    sprite_obs.commit_start_line=stream_stats.commit_start_line;sprite_obs.commit_end_line=stream_stats.commit_end_line;
    dok_nds_render_present();
    obs.presented_frame_id=obs.submitted_frame_id;row.presented_frame_id=obs.presented_frame_id;
    row.flags|=NDS_RENDER_FRAME_PRESENTED;observe(&stats);
    row.disp3d_flags=stats.disp3d_flags;row.gxstat_flags=stats.gxstat_flags;row.rdlines=stats.rdlines;
    row.camera_x=obs.camera_x=(uint32_t)camera.x;row.camera_y=obs.camera_y=(uint32_t)camera.y;
    row.camera_z=obs.camera_z=(uint32_t)camera.z;row.sin_q15=obs.sin_q15=(uint32_t)camera.sin_q15;
    row.cos_q15=obs.cos_q15=(uint32_t)camera.cos_q15;
    obs.render_frame_address=address(frame);obs.render_frame_bytes=sizeof(*frame);
    obs.cell_record_bytes=sizeof(dok_nds_render_cell);obs.quad_record_bytes=sizeof(dok_nds_render_quad);
    obs.render_generation=frame->generation;
    row.vblank_end=vblank_counter;
    row.total_ticks=present_ticks-(previous_present_ticks?previous_present_ticks:start);
    previous_present_ticks=present_ticks;previous_present_vblank=row.vblank_end;
    obs.flags=row.flags;
    if(row.vblank_end-row.vblank_start>obs.max_vblank_delta)
        obs.max_vblank_delta=row.vblank_end-row.vblank_start;
    if(row.total_ticks>obs.max_frame_ticks) obs.max_frame_ticks=row.total_ticks;
    ++obs.total_frames;history(&row);publish();
    last_render_stats=stats;
    return !obs.disp3d_sticky && !obs.gxstat_sticky;
}

int main(void)
{
    uint32_t index,current_level=0;
    obs.status=NDS_RENDER_BOOTING;obs.rdlines_min=UINT32_MAX;
    sprite_obs.trace_address=address((const void *)g_nds_sprite_trace);
    sprite_obs.trace_bytes=sizeof(NdsSpriteTrace);sprite_obs.trace_capacity=NDS_SPRITE_TRACE_CAPACITY;
    if(!memory_setup()) {obs.error_bits=1;goto done;}
    irqSet(IRQ_VBLANK,vblank_irq);irqEnable(IRQ_VBLANK);
    cpuStartTiming(0);obs.timer_hz=BUS_CLOCK;obs.timer_divider=1;
    if(dok_nds_clock_initialize(&platform)!=DOK_STATUS_OK){obs.error_bits=128;goto done;}
    obs.vblank_ticks=560190; /* 355*263*6 bus-clock ticks. */
    obs.history_address=address((const void *)g_nds_render_history);
    obs.history_capacity=NDS_RENDER_HISTORY_CAPACITY;
    obs.history_record_bytes=sizeof(NdsRenderHistoryRecord);
    obs.position_address=address((const void *)g_nds_render_positions);
    obs.position_record_bytes=sizeof(NdsRenderPositionRecord);
    obs.projection_address=address(dok_nds_render_projection());obs.projection_words=16;
    if(dok_nds_render_init()!=DOK_NDS_RENDER_OK) {obs.error_bits=2;goto done;}
    {
        static const dok_nds_render_camera controls[] = {
            {0,-300,0,0,32768}, {0,-300,0,23170,23170},
            {-113,-301,-217,-23170,23170}, {15120,-300,22800,32768,0},
            {20880,-300,3120,32768,0}, {17520,-300,28560,32768,0}
        };
        for(index=0;index<sizeof(controls)/sizeof(controls[0]);++index) {
            const dok_nds_render_camera *camera=&controls[index];
            int32_t ox=(camera->x/512)*512,oz=(camera->z/512)*512;
            if(!position(index+1,camera,ox,0,oz,ox-512,0,oz+2048) ||
               !position(index+1,camera,ox+512,0,oz,ox-512,0,oz+2048) ||
               !position(index+11,camera,ox,0,oz,ox+240,-951,oz+1440) ||
               !position(index+21,camera,ox,0,oz,ox+240,-561,oz+1440))
                {obs.error_bits=64;goto done;}
        }
    }
    hud_bg=bgInit(3,BgType_Bmp8,BgSize_B8_256x256,0,0);bgSetPriority(hud_bg,0);
    REG_BG0CNT=(REG_BG0CNT&~3u)|1u;
    consoleInit(&bottom,0,BgType_Text4bpp,BgSize_T_256x256,31,0,false,true);
    obs.vram_a=VRAM_A_CR;obs.vram_b=VRAM_B_CR;obs.vram_c=VRAM_C_CR;
    obs.vram_d=VRAM_D_CR;obs.vram_e=VRAM_E_CR;obs.vram_f=VRAM_F_CR;
    obs.vram_g=VRAM_G_CR;obs.vram_h=VRAM_H_CR;obs.vram_i=VRAM_I_CR;
    obs.texture_capacity_bytes=DOK_NDS_RENDER_TEXTURE_BYTES;obs.palette_capacity_bytes=16384;
    if(!fatInitDefault() || nds_asset_file_open(&package,"fat:/dok/assets.ndp",&reader)!=DOK_ASSET_OK ||
       dok_assets_open(&reader,&io->catalog,&session)!=DOK_ASSET_OK || !fixture_load())
        {obs.error_bits=4;goto done;}
    dok_render_scene_init(scene);
    for(index=0;index<fixture_count;++index) {
        const dok_render_case *c=&cases[index];uint32_t frame;
        obs.status=NDS_RENDER_RUNNING;obs.case_id=c->id;obs.level=c->level;publish();
        if(current_level!=c->level) {
            if(scene->leased) (void)dok_render_scene_release(scene,scene->generation);
            if(!level_load(c->level)) {obs.error_bits=8;goto done;}
            current_level=c->level;
        }
        begin_presentation_sequence(c);
        for(frame=0;frame<c->frames;++frame)
            if(!render_one(c,frame)) {obs.error_bits=16;goto done;}
        {
            NdsRenderHistoryRecord diagnostic={0};uint32_t start=previous_present_ticks;
            diagnostic.flags=NDS_RENDER_FRAME_DIAGNOSTIC;
            diagnostic.case_id=c->id;diagnostic.level=c->level;
            diagnostic.frame_id=obs.frame_id;diagnostic.presented_frame_id=obs.presented_frame_id;
            diagnostic.vblank_start=previous_present_vblank;
            obs.negative_pass_mask=255u;
            DC_FlushRange(storage,sizeof(*storage));
            DC_FlushRange(demand,sizeof(demand));
            DC_FlushRange(assets,obs.assets_high_water);
            diagnostics(&last_render_stats);
            DC_FlushRange(scene,sizeof(*scene));
            diagnostic.vblank_end=vblank_counter;diagnostic.total_ticks=cpuGetTiming()-start;
            history(&diagnostic);
        }
        obs.status=NDS_RENDER_READY;publish();
        do {swiWaitForVBlank();scanKeys();publish();} while(!(keysDown()&KEY_A));
        do {swiWaitForVBlank();scanKeys();} while(keysHeld()&KEY_A);
    }
    if(!memory_check()) obs.error_bits=32;
done:
    obs.status=obs.error_bits?NDS_RENDER_FAILED:NDS_RENDER_COMPLETE;publish();
    for(;;) {swiWaitForVBlank();publish();}
}
