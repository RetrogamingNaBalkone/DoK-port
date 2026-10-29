/* N07 static-world renderer diagnostic. No gameplay, saves, AI or audio. */
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
static dok_render_world *world;
static dok_render_workspace *scratch;
static dok_render_scene *scene;
static ProbeIO *io;
static NdsAssetFile package;
static dok_asset_session session;
static dok_nds_render_texture textures[DOK_NDS_RENDER_TEXTURE_CAPACITY];
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
       obs.static_end>DOK_NDS_MAIN_BASE+DOK_NDS_STATIC_CEILING) return 0;
    if(obs.heap_limit_before>DOK_NDS_ARM9_LIMIT &&
       reduceHeapSize(obs.heap_limit_before-DOK_NDS_ARM9_LIMIT)!=0) return 0;
    obs.heap_limit_policy=address(getHeapLimit());
    if(reduceHeapSize(DOK_NDS_RESERVE_BYTES)!=0) return 0;
    obs.malloc_request_bytes=DOK_NDS_SLAB_BYTES+31u;
    allocation=malloc(obs.malloc_request_bytes);
    if(!allocation) return 0;
    managed=(unsigned char *)(((uintptr_t)allocation+31u)&~(uintptr_t)31u);
    obs.managed_base=address(managed);obs.managed_bytes=DOK_NDS_SLAB_BYTES;
    cursor=0;world=(dok_render_world *)(void *)(managed+cursor);cursor+=DOK_NDS_WORLD_BYTES;
    cursor+=DOK_NDS_EVENTS_BYTES;
    scratch=(dok_render_workspace *)(void *)(managed+cursor);cursor+=DOK_NDS_SCRATCH_BYTES;
    scene=(dok_render_scene *)(void *)(managed+cursor);cursor+=DOK_NDS_SCENE_BYTES;
    assets=managed+cursor;cursor+=DOK_NDS_ASSETS_BYTES;
    cursor+=DOK_NDS_AUDIO_BYTES;
    io=(ProbeIO *)(void *)(managed+cursor);cursor+=DOK_NDS_IO_BYTES;
    if(cursor!=DOK_NDS_SLAB_BYTES) return 0;
    obs.world_used=obs.world_high_water=sizeof(*world);
    obs.scratch_used=obs.scratch_high_water=sizeof(*scratch);
    obs.scene_used=obs.scene_high_water=sizeof(*scene);
    return 1;
}

static int memory_check(void)
{
    uint32_t heap=address(getHeapEnd());
    obs.heap_end=heap;obs.heap_limit_final=address(getHeapLimit());
    if(heap<obs.static_end+DOK_NDS_SLAB_BYTES) return 0;
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
        dok_nds_render_clip_matrix(combined) != DOK_NDS_RENDER_OK) return 0;
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
    if(!file) return 0;
    fixture_bytes=(uint32_t)fread(io->fixture,1,sizeof(io->fixture),file);
    if(fgetc(file)!=EOF || ferror(file)) {fclose(file);return 0;}
    fclose(file);
    if(fixture_bytes<DOK_RENDER_FIXTURE_HEADER ||
       memcmp(io->fixture,"N07SCN1\0",8) || le32(io->fixture+8)!=1 ||
       le32(io->fixture+16)!=64 || le32(io->fixture+20)!=10240 ||
       le32(io->fixture+28)!=0) return 0;
    fixture_count=le32(io->fixture+12);
    expected=64u+fixture_count*64u+3u*10240u;
    if(!fixture_count || fixture_count>64u || fixture_bytes!=expected ||
       le32(io->fixture+24)!=fixture_bytes) return 0;
    dok_sha256_begin(&hash);dok_sha256_update(&hash,io->fixture+64,fixture_bytes-64);
    dok_sha256_end(&hash,digest);
    if(memcmp(digest,io->fixture+32,32)) return 0;
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
            return 0;
        for(j=0;j<i;++j) if(cases[j].id==c->id) return 0;
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
    printf("N07 DS 3D / LEVEL %lu\ncase %lu/%lu frame %lu\n\n",
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
    dok_nds_render_stats stats={0};NdsRenderHistoryRecord row={0};
    uint32_t start=cpuGetTiming(),i;
    row.flags=NDS_RENDER_FRAME_LOADING;row.case_id=obs.case_id;row.level=level;
    obs.flags=NDS_RENDER_FRAME_LOADING;
    row.vblank_start=vblank_counter;
    if(dok_render_world_load(&session,level,level,world,scratch,assets,
        DOK_NDS_ASSETS_BYTES)!=DOK_STATUS_OK) return 0;
    for(i=0;i<world->material_count;++i) {
        const dok_render_material *m=&world->materials[i];
        textures[i]=(dok_nds_render_texture){m->entry.id,m->pixels,m->entry.bytes,
            (const uint16_t *)(const void *)level_block(level),m->entry.storage_width,
            m->entry.storage_height,m->entry.width,m->entry.height,m->color0_transparent};
    }
    if(dok_nds_render_preload(textures,world->material_count,&stats)!=DOK_NDS_RENDER_OK)
        return 0;
    draw_hud(level);obs.texture_count=world->material_count;
    obs.assets_used=world->asset_bytes;
    if(obs.assets_used>obs.assets_high_water) obs.assets_high_water=obs.assets_used;
    obs.texture_bytes=stats.texture_bytes;obs.palette_bytes=stats.palette_bytes;
    obs.uploads_total+=stats.uploads;obs.upload_bytes_total+=stats.upload_bytes;
    row.uploads=stats.uploads;row.upload_bytes=stats.upload_bytes;
    row.texture_bytes=stats.texture_bytes;row.palette_bytes=stats.palette_bytes;
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

static int rejected_frame(const dok_nds_render_frame *frame,
                          dok_nds_render_result expected)
{
    dok_nds_render_stats candidate,old;
    memset(&candidate,0xa5,sizeof(candidate));old=candidate;
    return dok_nds_render_submit(frame,&candidate)==expected &&
        memcmp(&candidate,&old,sizeof(candidate))==0;
}

static uint32_t negative_admission(void)
{
    dok_nds_render_frame frame=scene->frame;
    dok_nds_render_cell cell=frame.cells[0],valid_cell;
    dok_nds_render_quad quad=cell.quads[0],valid_quad=quad;
    dok_nds_render_stats candidate,old;
    dok_nds_render_texture texture=textures[0];
    uint16_t palette[256];uint32_t mask=0;
    frame.cells=&cell;frame.cell_count=1;cell.quads=&quad;cell.quad_count=1;
    valid_cell=cell;
    quad.vertex[0].x=4096;
    if(rejected_frame(&frame,DOK_NDS_RENDER_RANGE)) mask|=1;
    quad=valid_quad;cell.origin_x++;
    if(rejected_frame(&frame,DOK_NDS_RENDER_RANGE)) mask|=2;
    cell=valid_cell;quad.texture_slot=DOK_NDS_RENDER_TEXTURE_CAPACITY;
    if(rejected_frame(&frame,DOK_NDS_RENDER_TEXTURE)) mask|=4;
    quad=valid_quad;quad.vertex[0].u_q16=-1;
    if(rejected_frame(&frame,DOK_NDS_RENDER_RANGE)) mask|=8;
    quad=valid_quad;quad.flags=1;
    if(rejected_frame(&frame,DOK_NDS_RENDER_TEXTURE)) mask|=16;
    quad=valid_quad;cell.quad_count=DOK_NDS_RENDER_POLYGON_LIMIT+1u;
    if(rejected_frame(&frame,DOK_NDS_RENDER_CAPACITY)) mask|=32;
    memset(&candidate,0xa5,sizeof(candidate));old=candidate;
    if(dok_nds_render_preload(textures,DOK_NDS_RENDER_TEXTURE_CAPACITY+1u,
        &candidate)==DOK_NDS_RENDER_CAPACITY &&
        memcmp(&candidate,&old,sizeof(candidate))==0) mask|=64;
    memcpy(palette,texture.palette,sizeof(palette));palette[0]|=0x8000u;
    texture.palette=palette;
    if(dok_nds_render_preload(&texture,1,&candidate)==DOK_NDS_RENDER_TEXTURE &&
        memcmp(&candidate,&old,sizeof(candidate))==0) mask|=128;
    return mask;
}

static int render_one(const dok_render_case *c,uint32_t ordinal)
{
    dok_nds_render_camera camera;
    const dok_nds_render_frame *frame=NULL;
    dok_nds_render_stats stats={0};NdsRenderHistoryRecord row={0};
    uint32_t start=cpuGetTiming(),phase=start,i,present_ticks;
    int64_t x=(int64_t)c->x+(int64_t)c->dx*ordinal;
    int64_t z=(int64_t)c->z+(int64_t)c->dz*ordinal;
    int64_t heading=((int64_t)c->heading+(int64_t)c->dheading*ordinal)%360;
    if(x<INT32_MIN || x>INT32_MAX || z<INT32_MIN || z>INT32_MAX) return 0;
    if(heading<0) heading+=360;
    camera=(dok_nds_render_camera){(int32_t)x,c->y,(int32_t)z,0,0};
    if(!dok_render_trig((uint32_t)heading,&camera.sin_q15,&camera.cos_q15)) return 0;
    row.vblank_start=previous_present_ticks?previous_present_vblank:vblank_counter;
    row.case_id=c->id;row.level=c->level;
    row.frame_id=++obs.frame_id;row.flags=(c->flags&DOK_RENDER_CASE_MOVING)?NDS_RENDER_FRAME_MOVING:0;
    if(c->flags&DOK_RENDER_CASE_WARMUP) row.flags|=NDS_RENDER_FRAME_WARMUP;
    if(scene->leased && dok_render_scene_release(scene,scene->generation)!=DOK_RENDER_SCENE_OK)
        return 0;
    if(dok_render_scene_prepare(scene,&world->initial,world->bindings,&camera,
        (uint32_t)heading,&frame)!=DOK_RENDER_SCENE_OK) return 0;
    obs.flat_diagnostic=(c->flags&DOK_RENDER_CASE_FLAT)?1u:0u;
    if(obs.flat_diagnostic) row.flags|=NDS_RENDER_FRAME_FLAT_DIAGNOSTIC;
    if(obs.flat_diagnostic) for(i=0;i<scene->counts.published_quads;++i) {
        dok_nds_render_quad *q=&scene->quads[i];
        uint32_t color=(q->source_surface_id*UINT32_C(2654435761))>>17;
        q->texture_slot=UINT16_MAX;q->color_rgb555=(uint16_t)(color?color:1u);
    }
    row.prepare_ticks=cpuGetTiming()-phase;phase=cpuGetTiming();
    if(dok_nds_render_submit(frame,&stats)!=DOK_NDS_RENDER_OK) return 0;
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
    present_ticks=wait_presentation_boundary();dok_nds_render_present();
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
    dok_asset_reader reader;
    uint32_t index,current_level=0;
    obs.status=NDS_RENDER_BOOTING;obs.rdlines_min=UINT32_MAX;
    if(!memory_setup()) {obs.error_bits=1;goto done;}
    irqSet(IRQ_VBLANK,vblank_irq);irqEnable(IRQ_VBLANK);
    cpuStartTiming(0);obs.timer_hz=BUS_CLOCK;obs.timer_divider=1;
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
            if(index==0) {
                obs.negative_pass_mask=negative_admission();
                if(obs.negative_pass_mask!=255u) {obs.error_bits=128;goto done;}
            }
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
