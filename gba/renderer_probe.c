#include "dok/renderer.h"
#include <stdint.h>
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
static dok_projection_camera camera;
static const uint8_t *font;
static uint8_t palette[768];
static unsigned white,black;
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

static void timer_start(void)
{
    T0C=0;T1C=0;T0D=0;T1D=0;T1C=0x84;T0C=0x80;
}
static uint32_t timer_stop(void)
{
    T0C=0;T1C=0;
    return (uint32_t)T0D | ((uint32_t)T1D << 16);
}

static void pixel(unsigned x,unsigned y,unsigned color)
{
    if (x < 240 && y < 160) g_render_frame.frame[y*240+x]=(uint8_t)color;
}
static void box(unsigned x,unsigned y,unsigned width,unsigned height,unsigned color)
{
    for (unsigned row=y;row<y+height && row<160;++row)
        for (unsigned col=x;col<x+width && col<240;++col) pixel(col,row,color);
}
static void label(unsigned x,unsigned y,const uint8_t *text)
{
    for (;*text && x+8<=240;++text,x+=8)
        for (unsigned row=0;row<8 && y+row<160;++row)
            for (unsigned col=0;col<8;++col)
                if (font[(unsigned)*text*8+row] & (128u>>col)) pixel(x+col,y+row,white);
}
static unsigned number(unsigned x,unsigned y,int value)
{
    uint8_t digits[8];unsigned count=0,n;
    if (value<0) {label(x,y,(const uint8_t *)"-");x+=8;n=0u-(unsigned)value;}
    else n=(unsigned)value;
    do {digits[count++]=(uint8_t)('0'+n%10);n/=10;} while(n && count<7);
    unsigned length=count;
    while(count) {uint8_t one[2]={digits[--count],0};label(x,y,one);x+=8;}
    return length*8;
}
static void hud(void)
{
    /* Compact G05 HUD, backed by actual player values. Full menus are G17. */
    box(0,0,56,12,black);label(2,2,(const uint8_t *)"HP");
    number(18,2,g_render_world.player.health);
    box(192,0,48,12,black);label(194,2,(const uint8_t *)"E");
    number(202,2,g_render_world.player.energy);
    if (g_render_world.player.keys) {
        box(192,132,48,12,black);label(194,134,(const uint8_t *)"K");
        number(202,134,g_render_world.player.keys);
    }
    box(0,144,240,16,black);
    static const uint8_t russian[]={0x93,0xe0,0xae,0xa2,0xa5,0xad,0xec,' ','1',0};
    label(0,148,g_render_diag.language ? (const uint8_t *)"Level 1" : russian);
}
static void load_ui(unsigned language)
{
    dok_rom_resource resource;uint16_t id;
    static const uint8_t name[]="FONT8";
    if (dok_rom_find(&id,&package,name,5)!=DOK_ROM_OK ||
        dok_rom_resource_at(&resource,&package,id,language)!=DOK_ROM_OK || resource.size!=2048) panic(0x91);
    font=resource.data;
    g_render_diag.font_ready=1;
    if (dok_renderer_palette(palette,&package,0,language)!=DOK_RENDER_OK) panic(0x92);
    unsigned darkest=~0u,brightest=0;
    for (unsigned i=0;i<256;++i) {
        unsigned r=palette[3*i],g=palette[3*i+1],b=palette[3*i+2];
        if (r>63 || g>63 || b>63) panic(0x93);
        PAL[i]=(uint16_t)((r>>1)|((g>>1)<<5)|((b>>1)<<10));
        unsigned brightness=r+g+b;
        if (brightness<darkest) {darkest=brightness;black=i;}
        if (brightness>brightest) {brightest=brightness;white=i;}
    }
    g_render_diag.palette_ready=1;
}

__attribute__((noreturn)) void gba_main(void)
{
    unsigned bss_ok=g_diag.magic==0 && g_vblanks==0 && g_render_diag.magic==0;
    DISPCNT=0x0080;
    g_diag.magic=0x444f4b31;g_diag.bss_ok=bss_ok;g_diag.data_ok=data_probe==0x1234abcd;
    if (!g_diag.bss_ok || !g_diag.data_ok) panic(1);
    if (gba_divmod_selftest()) panic(0x84);
    g_render_diag.magic=0x524e4435;
    g_render_diag.min_cycles=UINT32_MAX;
    g_render_diag.scene_height=144;g_render_diag.message_height=16;
    g_render_diag.frame_bytes=38400;g_render_diag.world_bytes=sizeof(g_render_world);
    g_render_diag.workspace_bytes=sizeof(g_render_frame);g_render_diag.cycle_budget=CYCLE_BUDGET;
    size_t size=(size_t)((uintptr_t)dok_gba_assets_end-(uintptr_t)dok_gba_assets);
    if (dok_rom_open(&package,dok_gba_assets,size)!=DOK_ROM_OK ||
        dok_rom_level(&g_render_view,&package,0,0)!=DOK_ROM_OK ||
        dok_runtime_reset(&g_render_world,&g_render_view)!=DOK_RUNTIME_OK) panic(0x80);
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
    unsigned visible=0;
    for (;;) {
        unsigned switched=0;
        g_diag.keys=(uint32_t)(~KEYINPUT)&0x3ff;g_diag.seen|=g_diag.keys;
        if ((g_diag.keys&12)==12) panic(0x10);
        if ((g_diag.keys&4) && !(g_render_diag.last_keys&4)) {
            unsigned language=g_render_diag.language^1;
            if (dok_rom_switch_language(&g_render_world,&g_render_view,&package,language)!=DOK_ROM_OK) panic(0x82);
            g_render_diag.language=language;g_render_diag.switches++;load_ui(language);
            switched=1;
        }
        g_render_diag.last_keys=g_diag.keys;
        uint32_t start_vblank=g_vblanks;
        g_render_diag.reserved3=1;
        timer_start();
        dok_renderer_status status=dok_renderer_draw(&g_render_frame,&g_render_world,&package,0,
            g_render_diag.language,&camera,&g_render_stats);
        g_render_diag.status=status;
        if (status!=DOK_RENDER_OK) panic(0xa0+(unsigned)status);
        hud();
        volatile uint16_t *page=visible ? PAGE0 : PAGE1;
        const uint8_t *frame=g_render_frame.frame;
        for (unsigned i=0;i<19200;++i) page[i]=(uint16_t)(frame[2*i]|((unsigned)frame[2*i+1]<<8));
        uint32_t cycles=timer_stop();
        g_render_diag.last_cycles=cycles;
        if (cycles<g_render_diag.min_cycles) g_render_diag.min_cycles=cycles;
        if (cycles>g_render_diag.max_cycles) g_render_diag.max_cycles=cycles;
        if (cycles>CYCLE_BUDGET) g_render_diag.over_budget++;
        g_render_diag.faces=g_render_stats.faces_drawn;g_render_diag.pixels=g_render_stats.pixels_written;
        g_render_diag.reserved1=g_render_frame.face_count;
        if (!g_render_diag.draws || switched)
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
    }
}
