#include "dok/renderer.h"
#include "dok/visual.h"
#include "dok/flow.h"
#include "dok/combat.h"
#include "dok/collision.h"
extern dok_combat g_combat;
extern unsigned gba_flow_active(void);
extern void gba_flow_update(void);
extern void gba_flow_draw(uint8_t *,const uint8_t *,const uint8_t *,unsigned,unsigned);
extern void gba_audio_quiet(void);
extern void gba_ai_quiet(void);
extern void gba_combat_quiet(void);
extern void gba_motion_quiet(void);
extern void gba_ui_reset(void);
extern void gba_ui_start(void);
extern unsigned gba_ui_input(uint16_t);
extern unsigned gba_ui_language(void);
extern uint16_t gba_ui_keys(uint16_t);
extern void gba_ui_draw(uint8_t *,const uint8_t *,const uint8_t *,unsigned,unsigned);
#include <stdint.h>
extern unsigned gba_time_start(void);
extern void gba_audio_start(const dok_rom_assets *,unsigned,unsigned);
extern void gba_audio_update(unsigned);
extern void gba_time_poll(void);
extern unsigned gba_motion_start(dok_runtime *);
extern void gba_motion_level(dok_runtime *);
extern void gba_motion_poll(uint16_t);
extern void gba_motion_cost(uint32_t);
extern int gba_items_stat(int16_t category,int16_t value);
extern int gba_items_pickup(void);
#include <stddef.h>

#define R16(a) (*(volatile uint16_t *)(a))
#define DISPCNT R16(0x04000000)
#define DISPSTAT R16(0x04000004)
#define VCOUNT R16(0x04000006)
#define KEYINPUT R16(0x04000130)
#define IE R16(0x04000200)
#define IF R16(0x04000202)
#define IME R16(0x04000208)
#define T0D R16(0x04000100)
#define T0C R16(0x04000102)
#define T1D R16(0x04000104)
#define T1C R16(0x04000106)
#define PAL ((volatile uint16_t *)0x05000000)
#define PAGE0 ((volatile uint16_t *)0x06000000)
#define PAGE1 ((volatile uint16_t *)0x0600a000)
#define FRAME_GUARD UINT32_C(0xd04fcafe)
#define CYCLE_BUDGET UINT32_C(561792)

struct diagnostics {uint32_t magic,frames,vblanks,keys,seen,fault,data_ok,bss_ok;};
volatile struct diagnostics g_diag __attribute__((section(".bss.diag")));
volatile uint32_t g_vblanks;
struct renderer_diagnostics {
    uint32_t magic,status,language,level,draws,last_cycles,min_cycles,max_cycles;
    uint32_t faces,columns,pixels,switches,last_keys,player_x,player_y,player_z;
    uint32_t scene_height,message_height,cycle_budget,over_budget,present_vblank;
    uint32_t frame_bytes,world_bytes,workspace_bytes,guard_ok,palette_ready,font_ready;
    uint32_t reserved0,reserved1,reserved2,reserved3,reserved4;
};
volatile struct renderer_diagnostics g_render_diag;
_Static_assert(sizeof(struct renderer_diagnostics)==128,"G05 diagnostic ABI");
_Static_assert(offsetof(dok_renderer_workspace,frame)==4,"G05 frame ABI");
_Static_assert(offsetof(dok_renderer_workspace,faces)==38408,"G05 faces ABI");
_Static_assert(sizeof(dok_runtime)==33864,"G05 ARM runtime ABI");
dok_runtime g_render_world;
dok_runtime_level g_render_view;
dok_renderer_workspace g_render_frame;
dok_renderer_stats g_render_stats;
static dok_rom_assets package;
extern void gba_combat_start(dok_runtime *,const dok_rom_assets *,unsigned,unsigned);
extern void gba_combat_update(dok_renderer_workspace *,unsigned);
extern void gba_combat_overlay(dok_renderer_workspace *);
extern void gba_ai_start(dok_runtime *,const dok_rom_assets *,unsigned,unsigned);
extern void gba_ai_poll(uint32_t,const uint8_t *);
extern void gba_ai_update(unsigned);
static dok_visual visual;
void gba_visual_remove(uint16_t id) {if(id<DOK_RUNTIME_MAX_OBJECTS)dok_scheduler_destroy(&visual.scheduler,id);}
static uint8_t visual_visible[DOK_RUNTIME_MAX_OBJECTS];
extern uint32_t gba_time_ticks(void);
volatile struct {uint32_t freeze,request,ack,tick,object,slot,loop;} g_visual_probe;
volatile struct {uint32_t magic,error,steps,completed,now,object,frame,active,deadline,resource,budget;} g_visual_diag;
static void visual_start(void) {
    gba_audio_start(&package,g_render_diag.level,g_render_diag.language);
    gba_combat_start(&g_render_world,&package,g_render_diag.level,g_render_diag.language);
    gba_ai_start(&g_render_world,&package,g_render_diag.level,g_render_diag.language);
    dok_visual_init(&visual,&g_render_world,&package,g_render_diag.level,g_render_diag.language,gba_time_ticks(),0,0);
    for(unsigned id=0;id<DOK_RUNTIME_MAX_OBJECTS;++id) {
        g_render_frame.image_known[id]=0;
        for(unsigned plane=0;plane<3;++plane) g_render_frame.surface_known[id][plane]=0;
    }
    g_visual_diag.magic=0x56495331;
}
void gba_visual_poll(uint32_t tick) {
    if(g_visual_probe.freeze || !visual.world) return;
    visual.language=g_render_diag.language;
    /* The previous scene's visibility lease is sampled in simulation time.
     * Catch-up never depends on how many frames were presented meanwhile. */
    for(unsigned id=0;id<g_render_world.object_count;++id)
        visual_visible[id]=(uint8_t)(g_render_frame.image_known[id] || g_render_frame.surface_known[id][0]);
    dok_visual_advance(&visual,tick,visual_visible);
    gba_ai_poll(tick,visual_visible);
}
static void visual_update(void) {
    visual.language=g_render_diag.language;
    if(g_visual_probe.request!=g_visual_probe.ack) {
        if(g_visual_probe.slot && !dok_visual_play(&visual,(uint16_t)g_visual_probe.object,(uint16_t)g_visual_probe.slot,(int)g_visual_probe.loop)) ++visual.errors;
        dok_visual_poll(&visual,g_visual_probe.tick);g_visual_probe.ack=g_visual_probe.request;
    }
    unsigned id=g_visual_probe.object;if(id>=g_render_world.object_count) id=0;
    g_visual_diag.error=visual.errors;g_visual_diag.steps=visual.steps;g_visual_diag.completed=visual.completed;
    g_visual_diag.now=visual.now;g_visual_diag.object=id;g_visual_diag.frame=g_render_world.objects[id].frame;
    g_visual_diag.active=visual.owners[id].count;g_visual_diag.deadline=visual.owners[id].events[0].deadline;
    g_visual_diag.resource=visual.resource[id];g_visual_diag.budget=g_render_world.objects[id].animation_budget;
}
static dok_projection_camera camera;
/* Host harness command mailbox: commit request last, acknowledge after setup.
 * This drives a test camera only; it never moves the runtime player. */
volatile struct {uint32_t request,ack,level;int32_t x,y,z,heading;} g_camera_probe;
/* Test-only capture aid: hold a committed frame for one extra VBlank.
 * Default zero; excluded from the CPU timer and ordinary cadence tests. */
volatile uint32_t g_camera_capture_hold;
volatile struct {uint32_t enable,object;int32_t x,y,z;} g_sprite_probe;
/* G12 test-only: commit request last; commands1=stat,2=current-cell pickup.
 * Executes the production context/sink and acknowledges before scene draw. */
volatile struct {uint32_t request,ack,command;int32_t category,value;uint32_t result;} g_items_probe;
static void items_update(void) {
    if(g_items_probe.request==g_items_probe.ack) return;
    int result=0;
    if(g_items_probe.command==1) result=gba_items_stat((int16_t)g_items_probe.category,(int16_t)g_items_probe.value);
    else if(g_items_probe.command==2) result=gba_items_pickup();
    g_items_probe.result=(uint32_t)result;g_items_probe.ack=g_items_probe.request;
}
static unsigned test_camera;
static const uint8_t *font,*font16;
static uint8_t palette[768];
static unsigned white,black;
/* ROM is immutable for the life of this cartridge. Validate both localized
 * views/resources at level load, then switch only already checked views. */
static dok_runtime_level localized_views[2];
static struct {const uint8_t *font,*font16;uint16_t colors[256];unsigned white,black;} localized_ui[2];
static volatile uint32_t data_probe=0x1234abcd;
extern const uint8_t dok_gba_assets[],dok_gba_assets_end[];
extern void gba_irq(void);
extern unsigned gba_divmod_selftest(void);

static uint32_t hash_bytes(const void *data,size_t size)
{
    const uint8_t *p=data;uint32_t hash=2166136261u;
    while(size--) hash=(hash^*p++)*16777619u;
    return hash;
}

static __attribute__((noreturn)) void panic(unsigned code)
{
    g_diag.fault=code;
    IME=0;
    DISPCNT=0x0404;
    PAL[0]=31;
    for (unsigned i=0;i<19200;++i) PAGE0[i]=0;
    for (;;) __asm__ volatile("nop");
}

static uint32_t timer_base;
static uint32_t timer_sample(void)
{
    uint16_t hi,lo,next;
    do {hi=R16(0x0400010c);lo=R16(0x04000108);next=R16(0x0400010c);} while(hi!=next);
    return ((((uint32_t)hi<<16)|lo)-timer_base)*64u;
}
static void timer_start(void){timer_base=0;timer_base=timer_sample()/64u;}
static uint32_t timer_stop(void){return timer_sample();}

volatile uint32_t g_render_stages[7];
void gba_render_stage(unsigned stage) {
    g_render_stages[stage]=timer_sample();
}
static void prepare_level(unsigned level)
{
    dok_rom_resource resource;uint16_t id;
    static const uint8_t name[]="FONT8";
    for(unsigned language=0;language<2;++language) {
        if(dok_rom_level(&localized_views[language],&package,level,language)!=DOK_ROM_OK) panic(0x95);
        if (dok_rom_find(&id,&package,name,5)!=DOK_ROM_OK ||
            dok_rom_resource_at(&resource,&package,id,language)!=DOK_ROM_OK || resource.size!=2048) panic(0x91);
        localized_ui[language].font=resource.data;
        if(dok_rom_find(&id,&package,(const uint8_t *)"FONT16",6)!=DOK_ROM_OK ||
           dok_rom_resource_at(&resource,&package,id,language)!=DOK_ROM_OK || resource.size!=4096) panic(0x91);
        localized_ui[language].font16=resource.data;
        if (dok_renderer_palette(palette,&package,level,language)!=DOK_RENDER_OK) panic(0x92);
        unsigned darkest=~0u,brightest=0;
        for (unsigned i=0;i<256;++i) {
            unsigned r=palette[3*i],g=palette[3*i+1],b=palette[3*i+2];
            if (r>63 || g>63 || b>63) panic(0x93);
            localized_ui[language].colors[i]=(uint16_t)((r>>1)|((g>>1)<<5)|((b>>1)<<10));
            unsigned brightness=r+g+b;
            if (brightness<darkest) {darkest=brightness;localized_ui[language].black=i;}
            if (brightness>brightest) {brightest=brightness;localized_ui[language].white=i;}
        }
    }
    /* dok_rom_open already verified the full numeric/common profile. These
     * identities additionally guard the cached pair used by this probe. */
    if(localized_views[0].maps!=localized_views[1].maps ||
       localized_views[0].initial_objects!=localized_views[1].initial_objects ||
       localized_views[0].initial_actors!=localized_views[1].initial_actors ||
       localized_views[0].initial_player!=localized_views[1].initial_player) panic(0x96);
}
static __attribute__((section(".iwram"),target("arm"))) void load_ui(unsigned language)
{
    font=localized_ui[language].font;font16=localized_ui[language].font16;white=localized_ui[language].white;black=localized_ui[language].black;
    for(unsigned i=0;i<256;++i) PAL[i]=localized_ui[language].colors[i];
    g_render_diag.font_ready=1;
    g_render_diag.palette_ready=1;
}

const dok_rom_assets *gba_flow_assets(void){return &package;}
unsigned gba_flow_level(void){return g_render_diag.level;}
void gba_flow_quiet(void){
 /* Release every occupied projectile before a borrowed context can expire. */
 dok_collision_overlay(0,0,0);
 gba_combat_quiet();g_render_frame.sprite_count=0;gba_audio_quiet();
 gba_ai_quiet();gba_motion_quiet();
 for(unsigned i=0;i<DOK_RUNTIME_MAX_OBJECTS;i++){dok_scheduler_destroy(&visual.scheduler,(uint16_t)i);visual.resource[i]=0;visual.loop[i]=0;visual_visible[i]=0;}
 g_visual_diag.active=0;g_visual_diag.deadline=0;gba_ui_reset();
}
void gba_flow_load(unsigned level,unsigned carry){
 (void)carry;
 dok_collision_overlay(0,0,0);gba_flow_quiet();
 prepare_level(level);g_render_view=localized_views[g_render_diag.language];
 if(dok_runtime_reset(&g_render_world,&g_render_view)!=DOK_RUNTIME_OK)panic(0x95);
 g_render_diag.level=level;load_ui(g_render_diag.language);test_camera=0;
 gba_motion_level(&g_render_world);visual_start();gba_ui_reset();
 if(dok_renderer_camera_from_runtime(&camera,&g_render_world)!=DOK_RENDER_OK)panic(0x98);
}

__attribute__((noreturn)) void gba_main(void)
{
    unsigned bss_ok=g_diag.magic==0 && g_vblanks==0 && g_render_diag.magic==0;
    DISPCNT=0x0080;
    g_diag.magic=0x444f4b31;g_diag.bss_ok=bss_ok;g_diag.data_ok=data_probe==0x1234abcd;
    if (!g_diag.bss_ok || !g_diag.data_ok) panic(1);
    if (gba_divmod_selftest()) panic(0x84);
    for(unsigned row=0;row<16;++row) {
        if(dok_renderer_light_row(1920+(int)row*120,1920)!=row ||
           (row && dok_renderer_light_row(1919+(int)row*120,1920)!=row-1)) panic(0x85);
    }
    if(dok_renderer_light_row(-1,1920)!=0 || dok_renderer_light_row(65535,1920)!=0 ||
       dok_renderer_light_row(32767,-1000)!=15 || dok_renderer_light_row(10000,1920)!=15) panic(0x85);
    g_render_diag.magic=0x524e4435;
    g_render_diag.min_cycles=UINT32_MAX;
    g_render_diag.scene_height=144;g_render_diag.message_height=16;
    g_render_diag.frame_bytes=38400;g_render_diag.world_bytes=sizeof(g_render_world);
    g_render_diag.workspace_bytes=sizeof(g_render_frame);g_render_diag.cycle_budget=CYCLE_BUDGET;
    size_t size=(size_t)((uintptr_t)dok_gba_assets_end-(uintptr_t)dok_gba_assets);
    if (dok_rom_open(&package,dok_gba_assets,size)!=DOK_ROM_OK) panic(0x80);
    prepare_level(0);g_render_view=localized_views[0];
    if (dok_runtime_reset(&g_render_world,&g_render_view)!=DOK_RUNTIME_OK) panic(0x80);
    if (dok_renderer_camera_from_runtime(&camera,&g_render_world)!=DOK_RENDER_OK) panic(0x81);
    g_render_diag.player_x=(uint32_t)camera.x;g_render_diag.player_y=(uint32_t)camera.y;
    g_render_diag.player_z=(uint32_t)camera.z;
    g_render_frame.frame_guard_before=FRAME_GUARD;g_render_frame.frame_guard_after=FRAME_GUARD;
    load_ui(0);
    /* Identity affine transform for the Mode4 background. */
    R16(0x04000020)=0x100;R16(0x04000022)=0;R16(0x04000024)=0;R16(0x04000026)=0x100;
    *(volatile uint32_t *)0x04000028=0;*(volatile uint32_t *)0x0400002c=0;
    *(void (** volatile)(void))0x03007ffc=gba_irq;
    R16(0x03007ff8)=0;IF=0x3fff;IE=1;DISPSTAT=8;IME=1;
    if (gba_time_start()) panic(0x90);
    if (gba_motion_start(&g_render_world)) panic(0x97);
    visual_start();
    gba_ui_start();
    unsigned visible=0;
    for (;;) {
        if(g_camera_probe.request!=g_camera_probe.ack) {
            test_camera=1;
            unsigned level=g_camera_probe.level;
            if(level>=3) panic(0x94);
            if(level!=g_render_diag.level) {
                gba_flow_load(level,0);test_camera=1;
            }
            camera.x=g_camera_probe.x;camera.y=g_camera_probe.y;camera.z=g_camera_probe.z;
            dok_renderer_camera_heading(&camera,g_camera_probe.heading);
            g_camera_probe.ack=g_camera_probe.request;
        }
        uint32_t start_vblank=g_vblanks;
        timer_start();
        g_diag.keys=(uint32_t)(~KEYINPUT)&0x3ff;g_diag.seen|=g_diag.keys;
        unsigned switched=gba_ui_input((uint16_t)g_diag.keys);
        gba_time_poll();
        uint32_t motion_start=timer_sample();
        if(gba_flow_active())gba_motion_poll(gba_ui_keys((uint16_t)g_diag.keys));
        if(!test_camera && dok_renderer_camera_from_runtime(&camera,&g_render_world)!=DOK_RENDER_OK) panic(0x98);
        gba_motion_cost(timer_sample()-motion_start);
        if(!test_camera) {
            g_render_diag.player_x=(uint32_t)camera.x;g_render_diag.player_y=(uint32_t)camera.y;
            g_render_diag.player_z=(uint32_t)camera.z;
        }
        if (switched) {
            unsigned language=gba_ui_language();
            g_render_view=localized_views[language];
            g_render_diag.language=language;g_render_diag.switches++;load_ui(language);
            switched=1;
        }
        g_render_diag.last_keys=g_diag.keys;
        gba_audio_update(g_render_diag.language);
        items_update();
        visual_update();
        gba_ai_update(g_render_diag.language);
        g_render_diag.reserved3=1;
        gba_combat_update(&g_render_frame,g_render_diag.language);
        gba_flow_update();
        if(g_sprite_probe.enable && g_render_frame.sprite_count<DOK_RENDER_SPRITE_CAPACITY) {
            unsigned n=g_render_frame.sprite_count++;
            g_render_frame.sprites[n]=(dok_renderer_sprite){g_sprite_probe.x,g_sprite_probe.y,g_sprite_probe.z,(uint16_t)g_sprite_probe.object,0};
            g_render_frame.sprite_resource[n]=0;g_render_frame.sprite_frame[n]=0;
        }
        dok_renderer_status status=dok_renderer_draw(&g_render_frame,&g_render_world,&package,g_render_diag.level,
            g_render_diag.language,&camera,&g_render_stats);
        g_render_diag.status=status;
        if (status!=DOK_RENDER_OK) panic(0xa0+(unsigned)status);
        gba_combat_overlay(&g_render_frame);gba_ui_draw(g_render_frame.frame,font,font16,white,black);gba_flow_draw(g_render_frame.frame,font,font16,white,black);gba_render_stage(5);
        volatile uint16_t *page=visible ? PAGE0 : PAGE1;
        /* DMA3 pauses the CPU, including audio IRQ refills. Bound each pause
         * while preserving every Mode4 pixel. DMA1 has higher bus priority. */
        for(unsigned offset=0;offset<9600;offset+=256){
            unsigned count=9600-offset;if(count>256)count=256;
            *(volatile uint32_t *)0x040000d4=(uintptr_t)g_render_frame.frame+offset*4;
            *(volatile uint32_t *)0x040000d8=(uintptr_t)page+offset*4;
            *(volatile uint32_t *)0x040000dc=0x84000000u|count;
            __asm__ volatile("nop" ::: "memory");
        }
        gba_render_stage(6);uint32_t cycles=timer_stop();
        g_render_diag.last_cycles=cycles;
        if (cycles<g_render_diag.min_cycles) g_render_diag.min_cycles=cycles;
        if (cycles>g_render_diag.max_cycles) g_render_diag.max_cycles=cycles;
        if (cycles>CYCLE_BUDGET) g_render_diag.over_budget++;
        g_render_diag.faces=g_render_stats.faces_drawn;g_render_diag.pixels=g_render_stats.pixels_written;
        g_render_diag.reserved1=g_render_frame.face_count;
        if (!g_render_diag.draws || switched || g_camera_probe.request)
            g_render_diag.reserved2=hash_bytes(g_render_frame.faces,
                (size_t)g_render_frame.face_count*sizeof(g_render_frame.faces[0]));
        g_render_diag.guard_ok=g_render_frame.frame_guard_before==FRAME_GUARD &&
            g_render_frame.frame_guard_after==FRAME_GUARD;
        if (!g_render_diag.guard_ok) panic(0x83);
        g_render_diag.reserved3=2;
        /* Present only a complete frame, at most once per two VBlanks.
         * Wait for a fresh VBlank when work has already exceeded its budget. */
        while ((uint32_t)(g_vblanks-start_vblank)<2) __asm__ volatile("nop");
        while (VCOUNT<160) {}
        visible^=1;DISPCNT=(uint16_t)(0x0404|(visible<<4));
        g_render_diag.reserved0=visible;
        g_render_diag.present_vblank=g_vblanks;g_render_diag.draws++;
        g_diag.frames++;g_diag.vblanks=g_vblanks;
        if(g_camera_capture_hold) {
            uint32_t captured_vblank=g_vblanks;
            while(g_vblanks==captured_vblank) __asm__ volatile("nop");
        }
    }
}
