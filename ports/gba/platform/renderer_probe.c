#include "dok/renderer.h"
#include "dok/visual.h"
#include "dok/flow.h"
#include "dok/combat.h"
#include "dok/collision.h"
#include "dok/ui.h"
extern dok_combat g_combat;
extern dok_ui g_ui;
extern unsigned gba_flow_active(void);
extern unsigned gba_flow_screen(void);
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
extern void gba_ui_snapshot(const uint8_t *,const uint8_t *);
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
#ifdef DOK_RENDER_HW_DIRECT_PROTOTYPE
dok_renderer_workspace g_render_frame __attribute__((aligned(8)));
#else
dok_renderer_workspace g_render_frame;
#endif
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
static dok_ui_art localized_art[2];
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
/* Completed frame snapshot stays valid while the next frame is rasterized.
 * Consumers measuring normal cadence must not require a capture-only hold. */
volatile uint32_t g_render_completed_stages[7];
#ifdef DOK_RENDER_HW_BG_PROTOTYPE
/* Coherent completed-draw counters; the next loop may already be in logic
 * when a libretro frame boundary returns to the profiling harness. */
volatile uint32_t g_hw_completed_logic[8];
extern volatile uint32_t g_time_diag[],g_motion_diag[],g_ai_diag[],g_combat_diag[],g_flow_diag[],g_ui_diag[];
#endif
void gba_render_stage(unsigned stage) {
    g_render_stages[stage]=timer_sample();
}
static void prepare_ui(void)
{
    dok_rom_resource resource;uint16_t id;
    static const uint8_t name[]="FONT8";
    for(unsigned language=0;language<2;++language) {
        if (dok_rom_find(&id,&package,name,5)!=DOK_ROM_OK ||
            dok_rom_resource_at(&resource,&package,id,language)!=DOK_ROM_OK || resource.size!=2048) panic(0x91);
        localized_ui[language].font=resource.data;
        if(dok_rom_find(&id,&package,(const uint8_t *)"FONT16",6)!=DOK_ROM_OK ||
           dok_rom_resource_at(&resource,&package,id,language)!=DOK_ROM_OK || resource.size!=4096) panic(0x91);
        localized_ui[language].font16=resource.data;
        /* Original CP866 resource names: panel, life, magic, copper/iron/gold. */
        static const uint8_t art_names[6][7]={
            "\217\200\215\205\213\234","\217\206\210\207\215\234","\217\214\200\203\210\237",
            "\217\212\213\236\227\214","\217\212\213\236\227\206","\217\212\213\236\227\207"};
        static const unsigned widths[6]={320,25,17,26,26,26},heights[6]={39,17,18,30,30,30};
        dok_ui_art *a=&localized_art[language];
        dok_image_view *images[6]={&a->panel,&a->health,&a->energy,&a->keys[0],&a->keys[1],&a->keys[2]};
        for(unsigned i=0;i<6;i++){
            if(dok_rom_find(&id,&package,art_names[i],6)!=DOK_ROM_OK ||
               dok_rom_resource_at(&resource,&package,id,language)!=DOK_ROM_OK ||
               dok_image_open(resource.data,resource.size,images[i])!=DOK_ASSET_OK ||
               images[i]->width!=widths[i] || images[i]->height!=heights[i])panic(0x91);
        }
        /* The build packs already reduced images; drawing only blits ROM
         * pixels. Original RES and the generic image decoder stay unchanged. */
        extern const uint8_t dok_ui_icons[2][610];
        static const unsigned icon_widths[5]={12,8,10,10,10},icon_heights[5]={8,8,12,12,12};
        unsigned icon_offset=0;
        for(unsigned i=0;i<5;i++){
            unsigned size=18+icon_widths[i]*icon_heights[i];
            if(icon_offset+size>610 || dok_image_open(dok_ui_icons[language]+icon_offset,size,images[i+1])!=DOK_ASSET_OK ||
               images[i+1]->width!=icon_widths[i] || images[i+1]->height!=icon_heights[i])panic(0x91);
            icon_offset+=size;
        }
        if(icon_offset!=610)panic(0x91);
        static const uint8_t splash_name[]="\207\200\221\222\200\202\212\200_\221\217\220";
        static const uint8_t splash_pal[]="\207\200\221\222\200\202\212\200_\217\200\213";
        static const uint8_t menu_pal[]="\214\205\215\236_\217\200\213";
        if(dok_rom_find(&id,&package,splash_name,sizeof(splash_name)-1)!=DOK_ROM_OK ||
           dok_rom_resource_at(&resource,&package,id,language)!=DOK_ROM_OK ||
           dok_image_open(resource.data,resource.size,&a->splash)!=DOK_ASSET_OK || a->splash.width!=320 || a->splash.height!=200)panic(0x91);
        if(dok_rom_find(&id,&package,(const uint8_t *)"FON",3)!=DOK_ROM_OK ||
           dok_rom_resource_at(&resource,&package,id,language)!=DOK_ROM_OK ||
           dok_image_open(resource.data,resource.size,&a->background)!=DOK_ASSET_OK || a->background.width!=46 || a->background.height!=46)panic(0x91);
        if(dok_rom_find(&id,&package,splash_pal,sizeof(splash_pal)-1)!=DOK_ROM_OK ||
           dok_rom_resource_at(&resource,&package,id,language)!=DOK_ROM_OK || resource.size!=768)panic(0x91);
        a->splash_palette=resource.data;
        if(dok_rom_find(&id,&package,menu_pal,sizeof(menu_pal)-1)!=DOK_ROM_OK ||
           dok_rom_resource_at(&resource,&package,id,language)!=DOK_ROM_OK || resource.size!=768)panic(0x91);
        a->menu_palette=resource.data;
        extern const uint8_t *const dok_ui_frontend_pixels[2][2];
        a->splash_frame=dok_ui_frontend_pixels[language][0];
        a->menu_frame=dok_ui_frontend_pixels[language][1];
        localized_ui[language].white=2;
        localized_ui[language].black=a->panel.pixels[5*320+10];
    }
}
static void prepare_level(unsigned level)
{
    if(dok_rom_level_pair(localized_views,&package,level)!=DOK_ROM_OK)panic(0x95);
    for(unsigned language=0;language<2;++language) {
        if (dok_renderer_palette(palette,&package,level,language)!=DOK_RENDER_OK) panic(0x92);
        for (unsigned i=0;i<256;++i) {
            unsigned r=palette[3*i],g=palette[3*i+1],b=palette[3*i+2];
            if (r>63 || g>63 || b>63) panic(0x93);
            localized_ui[language].colors[i]=(uint16_t)((r>>1)|((g>>1)<<5)|((b>>1)<<10));
        }
        /* Original green palette entry and the panel's own plain interior. */
        localized_ui[language].white=2;
        localized_ui[language].black=localized_art[language].panel.pixels[5*320+10];
    }
    /* Selected-pair validation above checks the shared numeric profile.
     * These identities additionally guard the cached pair used here. */
    if(localized_views[0].maps!=localized_views[1].maps ||
       localized_views[0].initial_objects!=localized_views[1].initial_objects ||
       localized_views[0].initial_actors!=localized_views[1].initial_actors ||
       localized_views[0].initial_player!=localized_views[1].initial_player) panic(0x96);
}
static __attribute__((section(".iwram"),target("arm"))) void load_ui(unsigned language)
{
    font=localized_ui[language].font;font16=localized_ui[language].font16;white=localized_ui[language].white;black=localized_ui[language].black;
    dok_ui_set_art(&localized_art[language]);
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
static unsigned world_started;
void gba_flow_load(unsigned level,unsigned carry){
 (void)carry;
 dok_collision_overlay(0,0,0);gba_flow_quiet();
 prepare_level(level);g_render_view=localized_views[g_render_diag.language];
 if(dok_runtime_reset(&g_render_world,&g_render_view)!=DOK_RUNTIME_OK)panic(0x95);
 g_render_diag.level=level;load_ui(g_render_diag.language);test_camera=0;
 if(!world_started){if(gba_motion_start(&g_render_world))panic(0x97);world_started=1;}
 else gba_motion_level(&g_render_world);
 visual_start();gba_ui_reset();
 if(dok_renderer_camera_from_runtime(&camera,&g_render_world)!=DOK_RENDER_OK)panic(0x98);
}

#ifdef DOK_RENDER_HW_OBJ_PROTOTYPE
#include "hardware_obj.inc"
/* Bitmap and tiled layouts overlap in VRAM. A complete black transition,
 * entered at a fresh VBlank, keeps uploads and palette changes off screen. */
static void video_layout_transition(void){
    while(VCOUNT>=160){}while(VCOUNT<160){}
    R16(0x04000054)=16;R16(0x04000050)=0xff;DISPCNT=0;
}
#ifdef DOK_RENDER_HW_WORLD_OBJ_PROBE_PROTOTYPE
#include "hardware_world_obj_probe.inc"
#endif
#endif

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
    if (dok_rom_open_catalog(&package,dok_gba_assets,size)!=DOK_ROM_OK) panic(0x80);
    prepare_ui();
    g_render_frame.frame_guard_before=FRAME_GUARD;g_render_frame.frame_guard_after=FRAME_GUARD;
    load_ui(0);
    /* Identity affine transform for the Mode4 background. */
    R16(0x04000020)=0x100;R16(0x04000022)=0;R16(0x04000024)=0;R16(0x04000026)=0x100;
    *(volatile uint32_t *)0x04000028=0;*(volatile uint32_t *)0x0400002c=0;
    *(void (** volatile)(void))0x03007ffc=gba_irq;
    R16(0x03007ff8)=0;IF=0x3fff;IE=1;DISPSTAT=8;IME=1;
    if (gba_time_start()) panic(0x90);
    gba_audio_start(&package,0,0);
    gba_ui_start();
    unsigned visible=0;
    uint32_t ui_key=UINT32_MAX,ui_messages=0,ui_page=0;
    for (;;) {
#ifdef DOK_RENDER_HW_OBJ_ROWS_PROBE_PROTOTYPE
        hw_obj_test_rows();
#endif
#ifdef DOK_RENDER_HW_WORLD_OBJ_PROBE_PROTOTYPE
        hw_world_obj_probe();
#endif
        unsigned camera_changed=0;
        if(g_camera_probe.request!=g_camera_probe.ack) {
            camera_changed=1;
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
        if(!test_camera && gba_flow_active())dok_ui_explore(&g_render_world);
        if(g_render_world.level && !test_camera && dok_renderer_camera_from_runtime(&camera,&g_render_world)!=DOK_RENDER_OK) panic(0x98);
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
#ifdef DOK_RENDER_HW_DIRECT_PROTOTYPE
        g_hw_tile_active=gba_flow_active();g_hw_weapon[4]=0;
        if(g_hw_tile_active) {hw_obj_black();g_hw_tile_black=hw_black_index;}
#ifdef DOK_RENDER_HW_BG_LOWERED_PROTOTYPE
        else hw_bg_hold_before_mode4();
#endif
#endif
#ifdef DOK_RENDER_SECTOR_GPU_PROTOTYPE
        if(g_hw_tile_active)gba_combat_overlay(&g_render_frame);
#endif
        unsigned frontend=dok_ui_frontend(&g_ui);
        unsigned text_screen=gba_flow_screen();
        dok_ui_tick(&g_ui,g_render_world.level,gba_time_ticks());
        uint32_t key=g_ui.state|(g_ui.focus<<4)|(g_ui.language<<8)|(g_ui.return_state<<12);
        uint32_t page_key=g_ui.page|(g_ui.active<<31);
        if(frontend && !text_screen && g_ui.state!=DOK_UI_STATUS && g_ui.state!=DOK_UI_MAP && key==ui_key &&
           g_ui.messages==ui_messages && page_key==ui_page){
            /* Static screens stay in VRAM. Poll every VBlank instead of
             * repainting and uploading the same38400 pixels between taps. */
            gba_ui_snapshot(font,font16);
            g_render_diag.last_cycles=timer_stop();g_render_diag.reserved3=2;
            while(VCOUNT>=160){}while(VCOUNT<160){}
            g_render_diag.present_vblank=g_vblanks;++g_render_diag.draws;
            ++g_diag.frames;g_diag.vblanks=g_vblanks;
            if(g_camera_capture_hold){uint32_t held=g_vblanks;while(g_vblanks==held){}}
            continue;
        }
        /* Flow text can retain the title UI state/focus. It must not seed
         * the static-menu cache, or returning from STORY leaves text in VRAM. */
        ui_key=(frontend && !text_screen)?key:UINT32_MAX;ui_messages=g_ui.messages;ui_page=page_key;
        dok_renderer_status status=(frontend || text_screen)?DOK_RENDER_OK:dok_renderer_draw(&g_render_frame,&g_render_world,&package,g_render_diag.level,
            g_render_diag.language,&camera,&g_render_stats);
        g_render_diag.status=status;
        if (status!=DOK_RENDER_OK) panic(0xa0+(unsigned)status);
#ifdef DOK_RENDER_HW_BG_PROTOTYPE
#ifdef DOK_RENDER_SECTOR_BITMAP_PROTOTYPE
        if(0 && g_hw_tile_active)hw_bg_prepare();
#else
        if(g_hw_tile_active)hw_bg_prepare();
#ifdef DOK_RENDER_SECTOR_GPU_PROTOTYPE
        if(g_hw_tile_active)hw_sector_gpu_prepare();
#endif
#endif
#endif
#ifdef DOK_RENDER_SECTOR_GPU_PROTOTYPE
        if(!g_hw_tile_active)gba_combat_overlay(&g_render_frame);
#else
        if(!frontend && !text_screen)gba_combat_overlay(&g_render_frame);
#endif
#ifdef DOK_RENDER_HW_DIRECT_PROTOTYPE
        if(g_hw_tile_active) hw_direct_ui_begin();
#endif
        gba_ui_draw(g_render_frame.frame,font,font16,white,black);gba_flow_draw(g_render_frame.frame,font,font16,white,black);
#ifdef DOK_RENDER_HW_OBJ_PROTOTYPE
        int hw_enabled=
#ifdef DOK_RENDER_SECTOR_BITMAP_PROTOTYPE
            g_hw_tile_active;
#else
            gba_flow_active();
#endif
        uint32_t hw_wait_cycles=0;
        if(hw_enabled) hw_obj_prepare();
#endif
        gba_render_stage(5);
        volatile uint16_t *page=visible ? PAGE0 : PAGE1;
        /* DMA3 pauses the CPU, including audio IRQ refills. Bound each pause
         * while preserving every Mode4 pixel. DMA1 has higher bus priority. */
#ifdef DOK_RENDER_HW_OBJ_PROTOTYPE
        if(hw_enabled) {
#ifdef DOK_RENDER_HW_PRESENT_PROTOTYPE
            unsigned upload_budget=hw_upload_budget();g_hw_present_diag[0]=upload_budget;
#ifdef DOK_RENDER_HW_EARLY_PRESENT_PROTOTYPE
            if(upload_budget!=0xffffffffu)upload_budget+=6000;
            g_hw_present_diag[0]=upload_budget;
#endif
            /* Large seeks or a complete atlas change may exceed every safe
             * window. Use the existing blanked transition instead of waiting
             * forever for an impossible deadline. Prepared map deltas remain
             * valid; reloading textures also makes this path self-contained. */
            if(upload_budget!=0xffffffffu && upload_budget>
#ifdef DOK_RENDER_HW_EARLY_PRESENT_PROTOTYPE
                84u*1232u
#else
                68u*1232u
#endif
            ) {hw_bg_stream_cold=1;upload_budget=0xffffffffu;g_hw_present_diag[0]=upload_budget;}
#endif
            uint32_t wait_stamp=timer_sample();
#ifdef DOK_RENDER_HW_EARLY_PRESENT_PROTOTYPE
            if(upload_budget!=0xffffffffu) {
                for(;;) {
                    unsigned line=VCOUNT,elapsed=(uint32_t)(g_vblanks-start_vblank);
                    if(line>=144 && (line<160?elapsed>=1:elapsed>=2) && (228-line)*1232>=upload_budget)break;
                }
                ++g_hw_present_diag[2];g_hw_present_diag[1]=VCOUNT;
            } else {
                while((uint32_t)(g_vblanks-start_vblank)<2) __asm__ volatile("nop");
                while(VCOUNT>=160) {}
                while(VCOUNT<160) {}
                g_hw_present_diag[1]=VCOUNT;
            }
#else
            while((uint32_t)(g_vblanks-start_vblank)<2) __asm__ volatile("nop");
#ifdef DOK_RENDER_HW_PRESENT_PROTOTYPE
            unsigned line=VCOUNT;
            if(line>=160 && (228-line)*1232>=upload_budget)++g_hw_present_diag[2];
            else {
                if(line>=160)++g_hw_present_diag[3];
                while(VCOUNT>=160) {}
                while(VCOUNT<160) {}
            }
            g_hw_present_diag[1]=VCOUNT;
#else
            while(VCOUNT>=160) {}
            while(VCOUNT<160) {}
#endif
#endif
            hw_wait_cycles=timer_sample()-wait_stamp;
            if(!hw_video_ready && (DISPCNT&7)==4)video_layout_transition();
            hw_obj_upload();
#ifdef DOK_RENDER_HW_BG_PROTOTYPE
            hw_wait_cycles+=g_hw_bg_diag[7];
            if(VCOUNT<160) {uint32_t stamp=timer_sample();while(VCOUNT<160){}hw_wait_cycles+=timer_sample()-stamp;}
#endif
        } else {
            if((DISPCNT&7)==1 || (DISPCNT&7)==2)video_layout_transition();
            hw_video_ready=0;
#ifdef DOK_RENDER_HW_BG_PROTOTYPE
            if(hw_bg_enabled)hw_bg_disable();
#endif
#ifdef DOK_RENDER_HW_UI_OBJ_PROTOTYPE
            if((DISPCNT&7)==1 || (DISPCNT&7)==2)
#else
            if((DISPCNT&7)==1)
#endif
            {DISPCNT=0;}
#endif
        for(unsigned offset=0;offset<9600;offset+=256){
            unsigned count=9600-offset;if(count>256)count=256;
            *(volatile uint32_t *)0x040000d4=(uintptr_t)g_render_frame.frame+offset*4;
            *(volatile uint32_t *)0x040000d8=(uintptr_t)page+offset*4;
            *(volatile uint32_t *)0x040000dc=0x84000000u|count;
            __asm__ volatile("nop" ::: "memory");
        }
#ifdef DOK_RENDER_HW_OBJ_PROTOTYPE
        }
#endif
        gba_render_stage(6);uint32_t cycles=timer_stop();
#ifdef DOK_RENDER_HW_OBJ_PROTOTYPE
        cycles-=hw_wait_cycles;g_render_stages[6]-=hw_wait_cycles;
#endif
        for(unsigned i=0;i<7;++i) g_render_completed_stages[i]=g_render_stages[i];
        g_render_diag.last_cycles=cycles;
        if (cycles<g_render_diag.min_cycles) g_render_diag.min_cycles=cycles;
        if (cycles>g_render_diag.max_cycles) g_render_diag.max_cycles=cycles;
        if (cycles>CYCLE_BUDGET) g_render_diag.over_budget++;
        g_render_diag.faces=g_render_stats.faces_drawn;g_render_diag.pixels=g_render_stats.pixels_written;
        g_render_diag.reserved1=g_render_frame.face_count;
        if (!g_render_diag.draws || switched || camera_changed)
            g_render_diag.reserved2=hash_bytes(g_render_frame.faces,
                (size_t)g_render_frame.face_count*sizeof(g_render_frame.faces[0]));
        g_render_diag.guard_ok=g_render_frame.frame_guard_before==FRAME_GUARD &&
            g_render_frame.frame_guard_after==FRAME_GUARD;
        if (!g_render_diag.guard_ok) panic(0x83);
        g_render_diag.reserved3=2;
        /* Present only a complete frame, at most once per two VBlanks.
         * Wait for a fresh VBlank when work has already exceeded its budget. */
#ifdef DOK_RENDER_HW_OBJ_PROTOTYPE
        if(hw_enabled) {visible=0;R16(0x04000050)=0;R16(0x04000054)=0;DISPCNT=
#ifdef DOK_RENDER_SECTOR_BITMAP_PROTOTYPE
            (uint16_t)(0x1444|(hw_bitmap_page<<4));
#else
#ifdef DOK_RENDER_SECTOR_GPU_PROTOTYPE
            0x1c42;
#else
#ifdef DOK_RENDER_HW_BG_PROTOTYPE
#ifdef DOK_RENDER_HW_UI_OBJ_PROTOTYPE
#ifdef DOK_RENDER_HW_LOW_ADAPTIVE_PROTOTYPE
            (g_hw_low_mode?0x1c62:0x1541);
#else
#ifdef DOK_RENDER_HW_BG_LOWERED_PROTOTYPE
            0x1c62;
#else
            0x1462;
#endif
#endif
#else
#ifdef DOK_RENDER_HW_WORLD_OBJ_PROTOTYPE
            0x3561;
#else
#ifdef DOK_RENDER_HW_ROWS96_PROTOTYPE
            0x3541;
#else
            0x1541;
#endif
#endif
#endif
#else
            0x1141;
#endif
#endif
#endif
        }
        else {
#endif
        while ((uint32_t)(g_vblanks-start_vblank)<(frontend || text_screen?1u:2u)) __asm__ volatile("nop");
        while (VCOUNT>=160) {}
        while (VCOUNT<160) {}
        const uint8_t *ui_pal=dok_ui_palette(&g_ui);
        for(unsigned i=0;i<256;++i)PAL[i]=ui_pal?(uint16_t)((ui_pal[i*3]>>1)|((ui_pal[i*3+1]>>1)<<5)|((ui_pal[i*3+2]>>1)<<10)):localized_ui[g_render_diag.language].colors[i];
        visible^=1;R16(0x04000050)=0;R16(0x04000054)=0;DISPCNT=(uint16_t)(0x0404|(visible<<4));
#ifdef DOK_RENDER_HW_OBJ_PROTOTYPE
        }
#endif
        g_render_diag.reserved0=visible;
#ifdef DOK_RENDER_HW_BG_PROTOTYPE
        g_hw_completed_logic[0]=g_time_diag[2];g_hw_completed_logic[1]=g_motion_diag[2];
        g_hw_completed_logic[2]=g_motion_diag[3];g_hw_completed_logic[3]=g_ai_diag[2];
        g_hw_completed_logic[4]=g_ai_diag[8];g_hw_completed_logic[5]=g_combat_diag[2];
        g_hw_completed_logic[6]=g_flow_diag[1];g_hw_completed_logic[7]=g_ui_diag[1];
#endif
        g_render_diag.present_vblank=g_vblanks;g_render_diag.draws++;
        g_diag.frames++;g_diag.vblanks=g_vblanks;
        if(g_camera_capture_hold) {
            uint32_t captured_vblank=g_vblanks;
            while(g_vblanks==captured_vblank) __asm__ volatile("nop");
        }
    }
}
