/* N02 platform diagnostics. This is an independent program, not the game loop. */
#include <nds.h>
#include <fat.h>

#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "diagnostic_state.h"

#define MARKER_PATH "fat:/dok/nds-diag.txt"
#define TONE_CHANNEL 8
#define TONE_FREQUENCY_HZ 440
#define PSG_PHASES_PER_PERIOD 8
#define BUTTON_COUNT 12

static const char marker_contents[] = "DOK_NDS_DIAG_V1\n";

typedef struct {
    uint32_t mask;
    const char *name;
} Button;

static const Button buttons[BUTTON_COUNT] = {
    {KEY_A, "A"}, {KEY_B, "B"}, {KEY_X, "X"}, {KEY_Y, "Y"},
    {KEY_L, "L"}, {KEY_R, "R"}, {KEY_UP, "Up"}, {KEY_DOWN, "Down"},
    {KEY_LEFT, "Left"}, {KEY_RIGHT, "Right"},
    {KEY_START, "Start"}, {KEY_SELECT, "Select"}
};

typedef enum {
    FILE_UNCHECKED,
    FILE_MOUNT_FAILED,
    FILE_MISSING,
    FILE_OPEN_FAILED,
    FILE_READ_FAILED,
    FILE_BAD_CONTENTS,
    FILE_OK
} FileResult;

typedef struct {
    bool mounted;
    FileResult result;
    int error;
    uint32_t bytes_read;
    uint32_t read_fnv1a;
    uint32_t read_us;
    uint32_t attempts;
} FileCheck;

typedef struct {
    uint32_t held;
    uint32_t down;
    uint32_t up;
    uint32_t last_down;
    uint32_t last_up;
    uint32_t seen_down;
    uint32_t seen_up;
    uint32_t down_count[BUTTON_COUNT];
    uint32_t up_count[BUTTON_COUNT];
    touchPosition touch;
    uint32_t touch_down_count;
    uint32_t touch_up_count;
    uint32_t frame_us;
    uint32_t frame_min_us;
    uint32_t frame_max_us;
    uint32_t work_us;
    uint32_t work_ticks;
    uint32_t work_max_us;
    uint32_t missed_vblanks;
    uint32_t loops;
    uint32_t tone_count;
    int tone_channel;
    uint64_t tone_deadline;
    FileCheck file;
} DiagState;

static PrintConsole top_console;
static PrintConsole bottom_console;
static volatile uint32_t vblank_count;
static DiagState diag;
_Alignas(32) volatile NdsDiagState g_nds_diag = {
    .magic = NDS_DIAG_MAGIC,
    .abi_version = NDS_DIAG_ABI_VERSION,
    .size_bytes = sizeof(NdsDiagState)
};

_Static_assert(sizeof(NdsDiagState) == 62 * sizeof(uint32_t), "N02 probe ABI size");

static void on_vblank(void)
{
    ++vblank_count;
}

/* The SDK counter is 64-bit and runs at BUS_CLOCK / 64, independently of RTC.
 * Do not take timer 2 away from it. CPU timing below uses ARM9 timers 0 and 1.
 * These conversions retain 64 bits for uptime (the SDK's helpers take u32).
 */
static uint64_t ticks_to_ms(uint64_t ticks)
{
    return ticks * 64000u / BUS_CLOCK;
}

static uint32_t ticks_to_us(uint64_t ticks)
{
    uint64_t us = ticks * 64000000u / BUS_CLOCK;
    return us > UINT32_MAX ? UINT32_MAX : (uint32_t)us;
}

static void line(PrintConsole *console, int row, const char *format, ...)
{
    char text[96];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    consoleSelect(console);
    consoleSetCursor(console, 0, row);
    /* Leave column 31 blank, avoiding automatic line wrap/scroll at the edge. */
    printf("%-31.31s", text);
}

static const char *file_result_name(FileResult result)
{
    switch (result) {
    case FILE_UNCHECKED: return "not checked";
    case FILE_MOUNT_FAILED: return "FAT/DLDI unavailable";
    case FILE_MISSING: return "marker missing";
    case FILE_OPEN_FAILED: return "marker open failed";
    case FILE_READ_FAILED: return "marker read failed";
    case FILE_BAD_CONTENTS: return "marker contents differ";
    case FILE_OK: return "OK: marker verified";
    }
    return "unknown result";
}

static void check_marker(FileCheck *check)
{
    uint64_t start = systemCounterGetTicks();
    ++check->attempts;
    check->error = 0;
    check->bytes_read = 0;
    check->read_fnv1a = 0;

    if (!check->mounted) {
        errno = 0;
        check->mounted = fatInitDefault();
        if (!check->mounted) {
            check->result = FILE_MOUNT_FAILED;
            check->error = errno;
            goto finished;
        }
    }

    errno = 0;
    FILE *file = fopen(MARKER_PATH, "rb");
    if (!file) {
        check->error = errno;
        check->result = errno == ENOENT ? FILE_MISSING : FILE_OPEN_FAILED;
        goto finished;
    }

    /* One extra byte detects trailing data. No file is ever opened for writing. */
    unsigned char contents[sizeof(marker_contents)];
    errno = 0;
    size_t got = fread(contents, 1, sizeof(contents), file);
    check->bytes_read = (uint32_t)got;
    check->read_fnv1a = UINT32_C(2166136261);
    for (size_t i = 0; i < got; ++i)
        check->read_fnv1a = (check->read_fnv1a ^ contents[i]) * UINT32_C(16777619);
    bool failed = ferror(file) != 0;
    int read_error = errno;
    if (fclose(file) != 0) {
        failed = true;
        if (!read_error)
            read_error = errno;
    }

    if (failed) {
        check->result = FILE_READ_FAILED;
        check->error = read_error;
    } else if (got != sizeof(marker_contents) - 1 ||
               memcmp(contents, marker_contents, sizeof(marker_contents) - 1)) {
        check->result = FILE_BAD_CONTENTS;
    } else {
        check->result = FILE_OK;
    }

finished:
    check->read_us = ticks_to_us(systemCounterGetTicks() - start);
    fprintf(stderr, "N02 file attempt=%" PRIu32 " result=%d errno=%d bytes=%" PRIu32
            " elapsed_us=%" PRIu32 " path=%s\n", check->attempts,
            (int)check->result, check->error, check->bytes_read,
            check->read_us, MARKER_PATH);
}

static void scan_input(void)
{
    scanKeys();
    diag.held = keysHeld();
    diag.down = keysDown();
    diag.up = keysUp();
    if (diag.down)
        diag.last_down = diag.down;
    if (diag.up)
        diag.last_up = diag.up;
    diag.seen_down |= diag.down;
    diag.seen_up |= diag.up;

    for (unsigned i = 0; i < BUTTON_COUNT; ++i) {
        if (diag.down & buttons[i].mask)
            ++diag.down_count[i];
        if (diag.up & buttons[i].mask)
            ++diag.up_count[i];
    }
    if (diag.held & KEY_TOUCH)
        touchRead(&diag.touch); /* Retain last position after release. */
    if (diag.down & KEY_TOUCH)
        ++diag.touch_down_count;
    if (diag.up & KEY_TOUCH)
        ++diag.touch_up_count;

    if (diag.down || diag.up) {
        fprintf(stderr, "N02 input down=%04" PRIx32 " held=%04" PRIx32
                " up=%04" PRIx32 " touch=%u,%u\n",
                diag.down, diag.held, diag.up,
                (unsigned)diag.touch.px, (unsigned)diag.touch.py);
    }
}

static void update_sound(uint64_t now)
{
    if ((diag.down & KEY_B) ||
        (diag.tone_channel >= 0 && now >= diag.tone_deadline)) {
        if (diag.tone_channel >= 0)
            soundKill(diag.tone_channel);
        diag.tone_channel = -1;
    }
    if (diag.down & KEY_A) {
        /* libnds forwards freq to the sound timer as a sample/step rate.
         * One PSG duty-wave period takes eight steps, so an audible 440 Hz
         * tone needs 3520 timer steps/s. The emulator PCM check verifies this.
         */
        diag.tone_channel = soundPlayPSGChannel(TONE_CHANNEL, DutyCycle_50,
                                               TONE_FREQUENCY_HZ * PSG_PHASES_PER_PERIOD,
                                               64, 64);
        diag.tone_deadline = now + systemCounterMsecsToTicks(250);
        ++diag.tone_count;
        fprintf(stderr, "N02 tone channel=%d frequency=440 duration_ms=250\n",
                diag.tone_channel);
    }
}

static void draw_button_rows(unsigned first, unsigned end)
{
    for (unsigned i = first; i < end; ++i) {
        line(&bottom_console, (int)i + 3, "%6s %c %10" PRIu32 " %10" PRIu32,
             buttons[i].name, diag.held & buttons[i].mask ? '*' : '.',
             diag.down_count[i], diag.up_count[i]);
    }
}

static void draw(uint64_t uptime, uint32_t vblanks)
{
    /* Polling/time have VBlank cadence. The human-readable dashboard is a
     * separate, slower presentation: six groups, at most eight rows per frame.
     * A complete dashboard refresh takes six VBlanks (~10 Hz). This keeps the
     * diagnostic console from determining the input sampling interval.
     */
    switch (diag.loops % 6) {
    case 0:
    line(&top_console, 0, "TOP: DoK N02 PLATFORM DIAG");
    line(&top_console, 1, "C11 / BlocksDS / %s", isDSiMode() ? "DSi MODE!" : "DS mode");
    line(&top_console, 2, "VBlank: %10" PRIu32, vblanks);
    line(&top_console, 3, "Uptime: %12" PRIu64 " ms", ticks_to_ms(uptime));
    line(&top_console, 4, "Frame:  %10" PRIu32 " us", diag.frame_us);
    line(&top_console, 5, "Min/max: %" PRIu32 "/%" PRIu32 " us",
         diag.frame_min_us == UINT32_MAX ? 0 : diag.frame_min_us, diag.frame_max_us);
    line(&top_console, 6, "CPU/work prev: %" PRIu32 " us", diag.work_us);
    line(&top_console, 7, "CPU/work max:  %" PRIu32 " us", diag.work_max_us);
    break;
    case 1:
    line(&top_console, 8, "Missed VBlanks: %" PRIu32, diag.missed_vblanks);
    line(&top_console, 9, "Wait/probe publish excluded.");
    line(&top_console, 10, "DLDI read: attempt %" PRIu32, diag.file.attempts);
    line(&top_console, 11, "%s", MARKER_PATH);
    line(&top_console, 12, "%s", file_result_name(diag.file.result));
    line(&top_console, 13, "errno=%d %s", diag.file.error,
         diag.file.error ? strerror(diag.file.error) : "");
    line(&top_console, 14, "Read bytes=%" PRIu32 " us=%" PRIu32,
         diag.file.bytes_read, diag.file.read_us);
    line(&top_console, 15, "%s", diag.file.result == FILE_MOUNT_FAILED ?
         "Check loader DLDI / FAT media." :
         diag.file.result == FILE_MISSING ? "Copy marker from diag/sdroot." :
         diag.file.result == FILE_BAD_CONTENTS ? "Marker must match exact bytes." : "");
    break;
    case 2:
    line(&top_console, 16, "Tone: %s  starts=%" PRIu32,
         diag.tone_channel < 0 ? "off" : "440 Hz", diag.tone_count);
    line(&top_console, 17, "A: 250 ms tone / B: stop");
    line(&top_console, 18, "X: reread marker");
    line(&top_console, 19, "Y: reset interval min/max");
    line(&top_console, 20, "Start/Select also counted.");
    line(&top_console, 21, "Read only. No game assets.");
    line(&top_console, 22, "Diagnostics, not the game.");
    break;
    case 3:
    line(&bottom_console, 0, "BOTTOM: BUTTONS + TOUCH");
    line(&bottom_console, 1, "Down:%04" PRIx32 " Up:%04" PRIx32 " (last)",
         diag.last_down, diag.last_up);
    line(&bottom_console, 2, "   Key H       Down         Up");
    draw_button_rows(0, 5);
    break;
    case 4:
    draw_button_rows(5, BUTTON_COUNT);
    line(&bottom_console, 15, "Touch: %s", diag.held & KEY_TOUCH ? "HELD" : "released");
    break;
    case 5:
    line(&bottom_console, 16, "Last pixel x=%3u y=%3u",
         (unsigned)diag.touch.px, (unsigned)diag.touch.py);
    line(&bottom_console, 17, "Raw x=%4u y=%4u",
         (unsigned)diag.touch.rawx, (unsigned)diag.touch.rawy);
    line(&bottom_console, 18, "Touch down=%" PRIu32 " up=%" PRIu32,
         diag.touch_down_count, diag.touch_up_count);
    line(&bottom_console, 19, "Seen down:%04" PRIx32 " up:%04" PRIx32,
         diag.seen_down, diag.seen_up);
    line(&bottom_console, 20, "All 12 buttons: mask 0x0FFF");
    line(&bottom_console, 21, "Touch included as 0x1000.");
    line(&bottom_console, 22, "Hold, release, then check counts");
    break;
    }
}

static void publish_observation(uint64_t now, uint64_t started, uint32_t vblanks)
{
    ++g_nds_diag.sequence;
    /* Publish the odd sequence before changing data visible through physical
     * main RAM. The emulator observer does not read the ARM9 data cache.
     */
    DC_FlushRange((const void *)&g_nds_diag.sequence, sizeof(g_nds_diag.sequence));
    g_nds_diag.vblanks = vblanks;
    g_nds_diag.loops = diag.loops;
    g_nds_diag.system_ticks_low = (uint32_t)now;
    g_nds_diag.system_ticks_high = (uint32_t)(now >> 32);
    uint64_t uptime_ms = ticks_to_ms(now - started);
    g_nds_diag.uptime_ms_low = (uint32_t)uptime_ms;
    g_nds_diag.uptime_ms_high = (uint32_t)(uptime_ms >> 32);
    g_nds_diag.frame_us = diag.frame_us;
    g_nds_diag.frame_min_us = diag.frame_min_us;
    g_nds_diag.frame_max_us = diag.frame_max_us;
    g_nds_diag.work_ticks = diag.work_ticks;
    g_nds_diag.work_us = diag.work_us;
    g_nds_diag.work_max_us = diag.work_max_us;
    g_nds_diag.missed_vblanks = diag.missed_vblanks;
    g_nds_diag.keys_held = diag.held;
    g_nds_diag.keys_down = diag.down;
    g_nds_diag.keys_up = diag.up;
    g_nds_diag.keys_seen_down = diag.seen_down;
    g_nds_diag.keys_seen_up = diag.seen_up;
    for (unsigned i = 0; i < BUTTON_COUNT; ++i) {
        g_nds_diag.button_down_count[i] = diag.down_count[i];
        g_nds_diag.button_up_count[i] = diag.up_count[i];
    }
    g_nds_diag.touch_held = (diag.held & KEY_TOUCH) != 0;
    g_nds_diag.touch_x = diag.touch.px;
    g_nds_diag.touch_y = diag.touch.py;
    g_nds_diag.touch_raw_x = diag.touch.rawx;
    g_nds_diag.touch_raw_y = diag.touch.rawy;
    g_nds_diag.touch_down_count = diag.touch_down_count;
    g_nds_diag.touch_up_count = diag.touch_up_count;
    g_nds_diag.storage_result = diag.file.result;
    g_nds_diag.storage_errno = (uint32_t)diag.file.error;
    g_nds_diag.storage_mounted = diag.file.mounted;
    g_nds_diag.storage_bytes_read = diag.file.bytes_read;
    g_nds_diag.storage_read_fnv1a = diag.file.read_fnv1a;
    g_nds_diag.storage_read_us = diag.file.read_us;
    g_nds_diag.storage_attempts = diag.file.attempts;
    g_nds_diag.audio_trigger_count = diag.tone_count;
    g_nds_diag.audio_active = diag.tone_channel >= 0;
    DC_FlushRange((const void *)&g_nds_diag, sizeof(g_nds_diag));
    ++g_nds_diag.sequence;
    DC_FlushRange((const void *)&g_nds_diag.sequence, sizeof(g_nds_diag.sequence));
}

int main(void)
{
    powerOn(POWER_ALL_2D);
    lcdMainOnTop();
    videoSetMode(MODE_0_2D);
    videoSetModeSub(MODE_0_2D);
    vramSetBankA(VRAM_A_MAIN_BG);
    vramSetBankC(VRAM_C_SUB_BG);
    consoleInit(&top_console, 0, BgType_Text4bpp, BgSize_T_256x256,
                22, 3, true, true);
    consoleClear();
    consoleInit(&bottom_console, 0, BgType_Text4bpp, BgSize_T_256x256,
                22, 3, false, true);
    consoleClear();
    consoleDebugInit(DebugDevice_NOCASH);
    irqSet(IRQ_VBLANK, on_vblank);
    irqEnable(IRQ_VBLANK);
    systemCounterSetup();
    uint64_t started = systemCounterGetTicks();
    soundEnable();
    diag.tone_channel = -1;
    diag.frame_min_us = UINT32_MAX;

    line(&top_console, 0, "TOP: DoK N02 PLATFORM DIAG");
    line(&top_console, 10, "Initializing FAT/DLDI...");
    line(&bottom_console, 0, "BOTTOM: BUTTONS + TOUCH");
    fprintf(stderr, "N02 start mode=%s marker=%s\n",
            isDSiMode() ? "DSi" : "DS", MARKER_PATH);
    check_marker(&diag.file);

    uint64_t previous_frame = 0;
    uint32_t previous_vblank = vblank_count;
    for (;;) {
        swiWaitForVBlank();
        cpuStartTiming(0);
        uint64_t now = systemCounterGetTicks();
        uint32_t vblanks = vblank_count;
        if (previous_frame) {
            diag.frame_us = ticks_to_us(now - previous_frame);
            if (diag.frame_us < diag.frame_min_us)
                diag.frame_min_us = diag.frame_us;
            if (diag.frame_us > diag.frame_max_us)
                diag.frame_max_us = diag.frame_us;
        }
        previous_frame = now;
        uint32_t elapsed_vblanks = vblanks - previous_vblank;
        if (elapsed_vblanks > 1)
            diag.missed_vblanks += elapsed_vblanks - 1;
        previous_vblank = vblanks;

        scan_input();
        update_sound(now);
        if (diag.down & KEY_X)
            check_marker(&diag.file);
        if (diag.down & KEY_Y) {
            diag.frame_min_us = UINT32_MAX;
            diag.frame_max_us = 0;
            diag.work_max_us = 0;
            diag.missed_vblanks = 0;
        }
        draw(now - started, vblanks);
        if (++diag.loops % 120 == 0) {
            fprintf(stderr, "N02 timing vblank=%" PRIu32 " uptime_ms=%" PRIu64
                    " frame_us=%" PRIu32 " work_us=%" PRIu32 " missed=%" PRIu32 "\n",
                    vblanks, ticks_to_ms(now - started), diag.frame_us,
                    diag.work_us, diag.missed_vblanks);
        }
        /* BUS_CLOCK ticks measure scan/UI/I/O work, including IRQs and file waits.
         * VBlank wait and the observation publication below are excluded.
         * This is not an instruction counter or a full-game performance claim.
         * The 32-bit SDK CPU timer wraps after ~128 s of uninterrupted work.
         */
        diag.work_ticks = cpuEndTiming();
        diag.work_us = (uint32_t)((uint64_t)diag.work_ticks * 1000000u / BUS_CLOCK);
        if (diag.work_us > diag.work_max_us)
            diag.work_max_us = diag.work_us;
        publish_observation(now, started, vblanks);
    }
}
