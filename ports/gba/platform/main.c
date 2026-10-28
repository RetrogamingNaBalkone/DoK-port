#include <stdint.h>

#define REG16(addr) (*(volatile uint16_t *)(addr))
#define DISPCNT REG16(0x04000000)
#define DISPSTAT REG16(0x04000004)
#define VCOUNT REG16(0x04000006)
#define KEYINPUT REG16(0x04000130)
#define IE REG16(0x04000200)
#define IF REG16(0x04000202)
#define IME REG16(0x04000208)
#define BG_PALETTE ((volatile uint16_t *)0x05000000)
#define PAGE0 ((volatile uint16_t *)0x06000000)
#define PAGE1 ((volatile uint16_t *)0x0600a000)
#define RGB(r,g,b) ((r) | ((g) << 5) | ((b) << 10))

/* Public diagnostic record: symbols/offsets are exported from the ELF by Python. */
struct diagnostics {
    uint32_t magic, frames, vblanks, keys, seen, fault, data_ok, bss_ok;
};
volatile struct diagnostics g_diag __attribute__((section(".bss.diag")));
volatile uint32_t g_vblanks;
static volatile uint32_t data_probe = 0x1234abcd;
extern void gba_irq(void);
#ifdef DOK_TIME_PROBE
extern unsigned gba_time_start(void);
extern void gba_time_poll(void);
extern uint32_t gba_time_ticks(void);
#endif
#ifdef DOK_RUNTIME_PROBE
extern unsigned gba_runtime_selftest(void);
#endif
#ifdef DOK_ASSETS_PROBE
extern unsigned gba_assets_selftest(void);
extern void gba_assets_input(uint32_t keys);
extern const char *gba_assets_language(void);
#endif

/* Original 5x7 diagnostic font; columns, low bit at top. */
static const uint8_t font[][5] = {
    {62,81,73,69,62},{0,66,127,64,0},{98,81,73,73,70},{34,65,73,73,54},
    {24,20,18,127,16},{39,69,69,69,57},{60,74,73,73,48},{1,113,9,5,3},
    {54,73,73,73,54},{6,73,73,41,30},
    {126,17,17,17,126},{127,73,73,73,54},{62,65,65,65,34},{127,65,65,34,28},
    {127,73,73,73,65},{127,9,9,9,1},{62,65,73,73,122},{127,8,8,8,127},
    {0,65,127,65,0},{32,64,65,63,1},{127,8,20,34,65},{127,64,64,64,64},
    {127,2,12,2,127},{127,4,8,16,127},{62,65,65,65,62},{127,9,9,9,6},
    {62,65,81,33,94},{127,9,25,41,70},{38,73,73,73,50},{1,1,127,1,1},
    {63,64,64,64,63},{31,32,64,32,31},{63,64,56,64,63},{99,20,8,20,99},
    {7,8,112,8,7},{97,81,73,69,67}
};

/* Mode 4 requires halfword writes, including individual pixel changes. */
static void pixel(volatile uint16_t *page, unsigned x, unsigned y, unsigned color)
{
    volatile uint16_t *p = &page[y * 120 + x / 2];
    uint16_t v = *p;
    *p = (uint16_t)((x & 1) ? ((v & 255) | (color << 8)) : ((v & 0xff00) | color));
}

static void rect(volatile uint16_t *page, unsigned x, unsigned y,
                 unsigned w, unsigned h, unsigned color)
{
    /* All callers use even x/w. */
    for (unsigned row = y; row < y + h; ++row)
        for (unsigned col = x / 2; col < (x + w) / 2; ++col)
            page[row * 120 + col] = (uint16_t)(color * 257);
}

static void text(volatile uint16_t *page, unsigned x, unsigned y, const char *s, unsigned color)
{
    for (; *s; ++s, x += 6) {
        int index = *s >= '0' && *s <= '9' ? *s - '0' :
                    (*s >= 'A' && *s <= 'Z' ? *s - 'A' + 10 : -1);
        if (index < 0) continue;
        for (unsigned col = 0; col < 5; ++col)
            for (unsigned row = 0; row < 7; ++row)
                if (font[index][col] & (1u << row)) pixel(page, x + col, y + row, color);
    }
}

static void hex(volatile uint16_t *page, unsigned x, unsigned y, uint32_t value)
{
    char s[9];
    for (unsigned i = 0; i < 8; ++i) s[i] = "0123456789ABCDEF"[(value >> (28 - 4*i)) & 15];
    s[8] = 0;
    text(page, x, y, s, 1);
}

static void screen(volatile uint16_t *page)
{
    static const char *const names[] = {"A","B","SEL","START","RIGHT","LEFT","UP","DOWN","R","L"};
    rect(page, 0, 0, 240, 160, 0);
    rect(page, 0, 0, 240, 24, 2);
#if defined(DOK_TIME_PROBE)
    text(page, 10, 8, "DOK  G06 TIME PROBE", 1);
#elif defined(DOK_ASSETS_PROBE)
    text(page, 10, 8, "DOK  BILINGUAL ASSETS", 1);
#else
    text(page, 10, 8, "DOK  GBA DIAGNOSTIC", 1);
#endif
    text(page, 10, 32, "FRAME", 1); hex(page, 70, 32, g_diag.frames);
    text(page, 10, 44, "VBLANK", 1); hex(page, 70, 44, g_diag.vblanks);
    text(page, 10, 56, "KEYS", 1); hex(page, 70, 56, g_diag.keys);
    for (unsigned i = 0; i < 10; ++i) {
        unsigned x = 10 + (i % 5) * 44, y = 76 + (i / 5) * 22;
        rect(page, x, y, 40, 18, (g_diag.keys & (1u << i)) ? 3 : 2);
        text(page, x + 2, y + 5, names[i], 1);
    }
#if defined(DOK_TIME_PROBE)
    text(page, 10, 126, "TICKS", 1);hex(page, 70, 126, gba_time_ticks());
    text(page, 10, 144, "G06 SCHEDULER CHECK", 1);
#elif defined(DOK_ASSETS_PROBE)
    text(page, 10, 126, "LANG", 1); text(page, 40, 126, gba_assets_language(), 1);
    text(page, 70, 126, "SELECT TO SWITCH", 1);
    text(page, 10, 144, "G03 ROM ASSET CHECK", 1);
#else
    text(page, 10, 126, "START SEL  PANIC TEST", 1);
    text(page, 10, 144, "G01 PLATFORM CHECK", 1);
#endif
    rect(page, 218, 144, 12, 8, (g_diag.frames & 16) ? 3 : 2);
}

static __attribute__((noreturn)) void panic(uint32_t code)
{
    g_diag.fault = code;
    IME = 0;
    DISPCNT = 0x0404;
    BG_PALETTE[0] = RGB(20,1,2);
    rect(PAGE0, 0, 0, 240, 160, 0);
    text(PAGE0, 18, 40, "DOK GBA PANIC", 1);
    text(PAGE0, 18, 64, "CODE", 1); hex(PAGE0, 60, 64, code);
    text(PAGE0, 18, 100, "RESET EMULATOR TO RESTART", 1);
    for (;;) __asm__ volatile("nop");
}

__attribute__((noreturn)) void gba_main(void)
{
    unsigned visible = 0;
    uint32_t previous = 0;
    /* Record before writing any BSS globals; probes really exercise startup. */
    uint32_t bss_ok = g_diag.magic == 0 && g_diag.frames == 0 && g_vblanks == 0;
    DISPCNT = 0x0080; /* Forced blank during initialization. */
    BG_PALETTE[0] = RGB(2,3,5);
    BG_PALETTE[1] = RGB(29,30,28);
    BG_PALETTE[2] = RGB(6,10,13);
    BG_PALETTE[3] = RGB(4,24,15);
    g_diag.magic = 0x444f4b31;
    g_diag.bss_ok = bss_ok;
    g_diag.data_ok = data_probe == 0x1234abcd;
    if (!g_diag.bss_ok || !g_diag.data_ok) panic(1);
#ifdef DOK_TIME_PROBE
    if (gba_time_start()) panic(0x90);
#endif
#ifdef DOK_RUNTIME_PROBE
    {
        unsigned result = gba_runtime_selftest();
        if (result) panic(0x20 + result);
    }
#endif
#ifdef DOK_ASSETS_PROBE
    {
        unsigned result = gba_assets_selftest();
        if (result) panic(0x40 + result);
    }
#endif
    screen(PAGE0);
    screen(PAGE1);
    IME = 0;
    *(void (** volatile)(void))0x03007ffc = gba_irq;
    REG16(0x03007ff8) = 0;
    IF = 0x3fff;
    IE = 1;
    DISPSTAT = 8; /* VBlank IRQ. */
    IME = 1;
    DISPCNT = 0x0404; /* Mode 4, BG2, page zero. */
    for (;;) {
        while (g_vblanks == previous) __asm__ volatile("nop");
        previous = g_vblanks;
        g_diag.vblanks = previous;
        g_diag.frames++;
#ifdef DOK_TIME_PROBE
        gba_time_poll();
#endif
        g_diag.keys = (uint32_t)(~KEYINPUT) & 0x3ff;
        g_diag.seen |= g_diag.keys;
        if ((g_diag.keys & 12) == 12) panic(0x10);
#ifdef DOK_ASSETS_PROBE
        gba_assets_input(g_diag.keys);
#endif
        screen(visible ? PAGE0 : PAGE1);
        /* Present completed back buffer only during VBlank. */
        while (VCOUNT >= 160) {}
        while (VCOUNT < 160) {}
        visible ^= 1;
        DISPCNT = (uint16_t)(0x0404 | (visible << 4));
    }
}
