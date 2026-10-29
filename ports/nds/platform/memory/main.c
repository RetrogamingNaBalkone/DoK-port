/* N03 memory-boundary experiment; no game subsystems or original assets. */
#include <nds.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dok/contracts.h"
#include "dok/memory.h"
#include "dok/memory_budget.h"
#include "memory_probe.h"

#define USER_STACK_BYTES 12288u
#define PAINT_PATTERN UINT32_C(0xa73cb652)
#define PAINT_ACTIVE_GUARD 256u
#define PAINT_BOTTOM_GUARD 64u

extern unsigned char __end__, __eheap_end, __itcm_start, __itcm_end;
extern unsigned char __dtcm_start, __dtcm_top, __sbss_end, __sp_usr;

enum {
    CHECK_MODE = 1u, CHECK_LINK = 2u, CHECK_RESERVE = 4u,
    CHECK_ALLOCATION = 8u, CHECK_HEAP = 16u, CHECK_ARENAS = 32u,
    CHECK_WORK_STACK = 64u, CHECK_CALL_STACK = 128u, CHECK_CONTRACTS = 256u
};

_Alignas(32) volatile NdsMemoryProbe g_nds_memory_probe = {
    .magic = NDS_MEMORY_MAGIC, .abi_version = NDS_MEMORY_ABI,
    .size_bytes = sizeof(NdsMemoryProbe)
};
static NdsMemoryProbe observation;
static dok_arena arenas[NDS_MEMORY_ARENAS];
static PrintConsole top_console, bottom_console;
static volatile uint32_t stack_exercise_sink;

static uint32_t address(const void *p) { return (uint32_t)(uintptr_t)p; }

static void check(uint32_t bit, int passed)
{
    if (passed)
        observation.tests_pass_mask |= bit;
    else
        observation.error_bits |= bit;
}

static uint32_t read_sp(void)
{
    uintptr_t result;
    __asm__ volatile ("mov %0, sp" : "=r" (result));
    return (uint32_t)result;
}

/* Only paint a proven free lower part of the USER stack. IRQ/SVC stacks,
 * active frames and DTCM data are excluded. Sentinel scanning is a pattern
 * watermark, not an exact instruction-by-instruction stack measurement.
 */
__attribute__((noinline)) static int paint_stack(void)
{
    uint32_t low = observation.user_stack_top - USER_STACK_BYTES;
    uint32_t sp = read_sp();
    observation.observed_sp = sp;
    if (low < observation.dtcm_sbss_end ||
        sp > observation.user_stack_top || sp < low + PAINT_ACTIVE_GUARD + PAINT_BOTTOM_GUARD)
        return 0;
    observation.paint_lo = low;
    observation.paint_hi = (sp - PAINT_ACTIVE_GUARD) & ~3u;
    for (volatile uint32_t *p = (volatile uint32_t *)(uintptr_t)low;
         (uintptr_t)p < observation.paint_hi; ++p)
        *p = PAINT_PATTERN;
    return 1;
}

static uint32_t paint_watermark(void)
{
    uint32_t p = observation.paint_lo;
    while (p < observation.paint_hi &&
           *(volatile uint32_t *)(uintptr_t)p == PAINT_PATTERN)
        p += 4;
    return observation.paint_hi - p;
}

__attribute__((noinline)) static void exercise_stack(uint32_t depth)
{
    volatile uint32_t local[32];
    for (uint32_t i = 0; i < 32; ++i)
        local[i] = depth + i;
    if (depth)
        exercise_stack(depth - 1u);
    stack_exercise_sink += local[depth & 31u];
}

static void check_call_stack(void)
{
    int painted = paint_stack();
    if (!painted) {
        check(CHECK_CALL_STACK, 0);
        return;
    }
    observation.paint_watermark_before = paint_watermark();
    exercise_stack(5);
    observation.paint_watermark_after = paint_watermark();
    observation.paint_guard_ok = 1;
    for (uint32_t p = observation.paint_lo;
         p < observation.paint_lo + PAINT_BOTTOM_GUARD; p += 4)
        if (*(volatile uint32_t *)(uintptr_t)p != PAINT_PATTERN)
            observation.paint_guard_ok = 0;
    check(CHECK_CALL_STACK,
          observation.paint_guard_ok &&
          observation.paint_watermark_after > observation.paint_watermark_before &&
          observation.paint_watermark_after < observation.paint_hi - observation.paint_lo);
}

static int check_arenas(uint8_t *slab)
{
    static const uint32_t capacities[NDS_MEMORY_ARENAS] = {
        DOK_NDS_WORLD_BYTES, DOK_NDS_EVENTS_BYTES, DOK_NDS_SCRATCH_BYTES,
        DOK_NDS_SCENE_BYTES, DOK_NDS_ASSETS_BYTES, DOK_NDS_AUDIO_BYTES, DOK_NDS_IO_BYTES
    };
    uint32_t offset = 0;
    int passed = 1;
    for (uint32_t i = 0; i < NDS_MEMORY_ARENAS; ++i) {
        dok_arena *a = &arenas[i];
        void *allocation = NULL;
        void *failed_output = slab;
        passed &= dok_arena_init(a, slab + offset, capacities[i]) == DOK_STATUS_OK;
        passed &= dok_arena_alloc(a, capacities[i], 32, &allocation) == DOK_STATUS_OK;
        passed &= allocation == slab + offset;
        if (allocation != NULL) {
            uint8_t *bytes = allocation;
            bytes[0] = (uint8_t)(0x10u + i);
            bytes[capacities[i] - 1u] = (uint8_t)(0xe0u + i);
            passed &= dok_arena_alloc(a, 1, 1, &failed_output) == DOK_STATUS_NO_SPACE;
            passed &= failed_output == slab && a->used == capacities[i] &&
                      a->high_water == capacities[i] && a->failures == 1 &&
                      bytes[0] == (uint8_t)(0x10u + i) &&
                      bytes[capacities[i] - 1u] == (uint8_t)(0xe0u + i);
        }
        passed &= dok_arena_reset(a) == DOK_STATUS_OK;
        offset += capacities[i];
    }
    return passed && offset == DOK_NDS_SLAB_BYTES;
}

static void check_work_stack(void)
{
    dok_work_stack stack;
    void *backing = NULL;
    uint32_t output = 0x11223344u, value;
    int passed = dok_arena_alloc(&arenas[2], 32, 4, &backing) == DOK_STATUS_OK;
    if (!passed) { check(CHECK_WORK_STACK, 0); return; }
    passed &= dok_work_stack_init(&stack, backing, 32, sizeof(uint32_t)) == DOK_STATUS_OK;
    for (value = 1; value <= 8; ++value)
        passed &= dok_work_stack_push(&stack, &value) == DOK_STATUS_OK;
    passed &= dok_work_stack_push(&stack, &value) == DOK_STATUS_NO_SPACE;
    passed &= stack.depth == 8 && stack.high_water == 8 && stack.failures == 1;
    for (value = 8; value != 0; --value) {
        passed &= dok_work_stack_pop(&stack, &output) == DOK_STATUS_OK;
        passed &= output == value;
    }
    output = 0x11223344u;
    passed &= dok_work_stack_pop(&stack, &output) == DOK_STATUS_INVALID_STATE;
    passed &= output == 0x11223344u && stack.depth == 0 && stack.high_water == 8;
    observation.work_stack_capacity = stack.capacity;
    observation.work_stack_depth = stack.depth;
    observation.work_stack_high_water = stack.high_water;
    observation.work_stack_failures = stack.failures;
    passed &= dok_arena_reset(&arenas[2]) == DOK_STATUS_OK;
    check(CHECK_WORK_STACK, passed);
}

static uint64_t read_counter(void *context)
{
    (void)context;
    return systemCounterGetTicks();
}

static void check_contracts(void)
{
    dok_game_state game = {0};
    dok_game_info info = {0};
    const dok_game_config config = {
        .platform = { .read_counter = read_counter, .counter_hz_numerator = BUS_CLOCK,
                      .counter_hz_denominator = 64 },
        .assets = { .identity = 1, .revision = 1 },
        .balance = { .identity = 1, .revision = 1 }
    };
    int passed = dok_game_initialize(&game, &config) == DOK_STATUS_OK;
    passed &= dok_game_reset(&game) == DOK_STATUS_OK;
    passed &= dok_game_inspect(&game, &info) == DOK_STATUS_OK;
    observation.game_state_bytes = sizeof(game);
    observation.game_lifecycle_generation = info.lifecycle_generation;
    passed &= info.has_world == 0 && info.world_epoch == 0 && info.lifecycle_generation == 2;
    passed &= dok_game_shutdown(&game) == DOK_STATUS_OK;
    check(CHECK_CONTRACTS, passed);
}

static void memory_checks(void)
{
    uint8_t *raw, *slab;
    uint32_t delta;
    observation.is_dsi = isDSiMode();
    observation.region_base = DOK_NDS_MAIN_BASE;
    observation.region_limit = address(&__eheap_end);
    observation.region_bytes = observation.region_limit - observation.region_base;
    observation.static_end = address(&__end__);
    observation.static_bytes = observation.static_end - observation.region_base;
    observation.static_ceiling = DOK_NDS_STATIC_CEILING;
    observation.slab_bytes = DOK_NDS_SLAB_BYTES;
    observation.reserve_bytes = DOK_NDS_RESERVE_BYTES;
    observation.sdk_heap_min = DOK_NDS_SDK_HEAP_MIN;
    observation.heap_start = address(getHeapStart());
    observation.heap_end_before = address(getHeapEnd());
    observation.heap_limit_before = address(getHeapLimit());
    observation.itcm_base = address(&__itcm_start);
    observation.itcm_end = address(&__itcm_end);
    observation.itcm_used = observation.itcm_end - observation.itcm_base;
    observation.dtcm_base = address(&__dtcm_start);
    observation.dtcm_top = address(&__dtcm_top);
    observation.dtcm_sbss_end = address(&__sbss_end);
    observation.user_stack_top = address(&__sp_usr);
    observation.user_stack_bytes = USER_STACK_BYTES;
    observation.cp15_dtcm = CP15_GetDTCM();
    observation.cp15_itcm = CP15_GetITCM();
    check(CHECK_MODE, !observation.is_dsi);
    check(CHECK_LINK, observation.region_bytes == DOK_NDS_ARM9_BYTES &&
          observation.region_limit == DOK_NDS_ARM9_LIMIT &&
          observation.heap_start >= observation.static_end &&
          observation.static_bytes <= DOK_NDS_STATIC_CEILING);
    /* CRT initializes fake_heap_end from detected physical RAM, not the
     * conservative linker image region. Exclude any runtime extension first;
     * the emergency reserve must remain INSIDE our accepted ARM9 envelope. */
    if (observation.heap_limit_before < observation.region_limit) {
        check(CHECK_RESERVE, 0);
        return;
    }
    observation.sdk_limit_exclusion_bytes = observation.heap_limit_before - observation.region_limit;
    int policy_reduced = observation.sdk_limit_exclusion_bytes ?
        reduceHeapSize(observation.sdk_limit_exclusion_bytes) : 0;
    observation.heap_limit_policy = address(getHeapLimit());
    if (policy_reduced != 0 || observation.heap_limit_policy != observation.region_limit) {
        check(CHECK_RESERVE, 0);
        return;
    }
    int reduced = reduceHeapSize(DOK_NDS_RESERVE_BYTES);
    observation.heap_end_reduced = address(getHeapEnd());
    observation.heap_limit_reduced = address(getHeapLimit());
    check(CHECK_RESERVE, reduced == 0 &&
          observation.heap_limit_policy - observation.heap_limit_reduced == DOK_NDS_RESERVE_BYTES &&
          observation.heap_limit_reduced == observation.region_limit - DOK_NDS_RESERVE_BYTES &&
          observation.heap_end_reduced <= observation.heap_limit_reduced);
    if (observation.error_bits != 0)
        return;
    observation.malloc_request_bytes = DOK_NDS_SLAB_BYTES + 31u;
    raw = malloc(observation.malloc_request_bytes);
    observation.malloc_raw = address(raw);
    observation.heap_end_allocated = address(getHeapEnd());
    if (raw == NULL) { check(CHECK_ALLOCATION, 0); return; }
    /* malloc storage has no declared type. This alignment creates main-RAM
     * byte spans; future typed subsystems still own their type/lifetime rules. */
    slab = (uint8_t *)(((uintptr_t)raw + 31u) & ~(uintptr_t)31u);
    observation.slab_base = address(slab);
    check(CHECK_ALLOCATION, observation.slab_base >= observation.malloc_raw &&
          observation.slab_base + DOK_NDS_SLAB_BYTES <= observation.malloc_raw + observation.malloc_request_bytes &&
          observation.slab_base + DOK_NDS_SLAB_BYTES <= observation.heap_end_allocated);
    delta = observation.heap_end_allocated - observation.heap_start;
    if (delta >= DOK_NDS_SLAB_BYTES)
        observation.startup_heap_overhead = delta - DOK_NDS_SLAB_BYTES;
    else
        observation.startup_heap_overhead = UINT32_MAX;
    observation.static_and_overhead = observation.static_bytes + observation.startup_heap_overhead;
    observation.sdk_heap_available = observation.heap_limit_reduced - observation.heap_end_allocated;
    if (observation.static_and_overhead <= DOK_NDS_STATIC_CEILING)
        observation.code_envelope_headroom = DOK_NDS_STATIC_CEILING - observation.static_and_overhead;
    check(CHECK_HEAP, delta >= DOK_NDS_SLAB_BYTES &&
          observation.static_and_overhead <= DOK_NDS_STATIC_CEILING &&
          observation.sdk_heap_available >= DOK_NDS_SDK_HEAP_MIN);
    if (observation.error_bits != 0)
        return;
    memset(slab, 0xa5, DOK_NDS_SLAB_BYTES);
    for (uint32_t i = 0; i < DOK_NDS_SLAB_BYTES; ++i)
        if (((volatile uint8_t *)slab)[i] == 0xa5)
            ++observation.touched_bytes;
    check(CHECK_ALLOCATION, observation.touched_bytes == DOK_NDS_SLAB_BYTES);
    check(CHECK_ARENAS, check_arenas(slab));
    check_work_stack();
    check_contracts();
    /* The read-only observer, like DMA/ARM7, reads physical main RAM. Publish
     * the owned byte-span explicitly; do not rely on console cache effects. */
    DC_FlushRange(slab, DOK_NDS_SLAB_BYTES);
    /* The one platform-owned startup allocation remains live for this ROM.
     * No allocator is called per frame. A production shutdown will free it. */
}

static void publish(void)
{
    ++g_nds_memory_probe.sequence;
    DC_FlushRange((const void *)&g_nds_memory_probe.sequence, sizeof(uint32_t));
    uint32_t seq = g_nds_memory_probe.sequence;
    observation.magic = NDS_MEMORY_MAGIC;
    observation.abi_version = NDS_MEMORY_ABI;
    observation.size_bytes = sizeof(observation);
    observation.sequence = seq;
    for (uint32_t i = 0; i < NDS_MEMORY_ARENAS; ++i) {
        observation.arenas[i].capacity = arenas[i].capacity;
        observation.arenas[i].used = arenas[i].used;
        observation.arenas[i].high_water = arenas[i].high_water;
        observation.arenas[i].failures = arenas[i].failures;
    }
    memcpy((void *)&g_nds_memory_probe, &observation, sizeof(observation));
    DC_FlushRange((const void *)&g_nds_memory_probe, sizeof(g_nds_memory_probe));
    ++g_nds_memory_probe.sequence;
    DC_FlushRange((const void *)&g_nds_memory_probe.sequence, sizeof(uint32_t));
}

static void final_heap_check(void)
{
    observation.heap_end_final = address(getHeapEnd());
    observation.heap_limit_final = address(getHeapLimit());
    uint32_t total = observation.heap_end_final - observation.heap_start;
    if (total < DOK_NDS_SLAB_BYTES || observation.heap_limit_final < observation.heap_end_final) {
        check(CHECK_HEAP, 0);
        return;
    }
    observation.startup_heap_overhead = total - DOK_NDS_SLAB_BYTES;
    observation.static_and_overhead = observation.static_bytes + observation.startup_heap_overhead;
    observation.code_envelope_headroom = observation.static_and_overhead <= DOK_NDS_STATIC_CEILING ?
        DOK_NDS_STATIC_CEILING - observation.static_and_overhead : 0;
    observation.sdk_heap_available = observation.heap_limit_final - observation.heap_end_final;
    check(CHECK_HEAP, observation.static_and_overhead <= DOK_NDS_STATIC_CEILING &&
          observation.sdk_heap_available >= DOK_NDS_SDK_HEAP_MIN &&
          observation.heap_limit_final == observation.heap_limit_reduced);
    observation.status = observation.error_bits ? 2u : 1u;
}

int main(void)
{
    /* Exclude the reserve before console/stdio or our own dynamic allocation. */
    memory_checks();
    check_call_stack();
    observation.status = observation.error_bits ? 2u : 1u;
    powerOn(POWER_ALL_2D);
    lcdMainOnTop();
    videoSetMode(MODE_0_2D);
    videoSetModeSub(MODE_0_2D);
    vramSetBankA(VRAM_A_MAIN_BG);
    vramSetBankC(VRAM_C_SUB_BG);
    consoleInit(&top_console, 0, BgType_Text4bpp, BgSize_T_256x256, 22, 3, true, true);
    consoleInit(&bottom_console, 0, BgType_Text4bpp, BgSize_T_256x256, 22, 3, false, true);
    consoleSelect(&top_console);
    /* Warm up stdio before measuring the complete platform startup. The
     * malloc break is not a count of free chunks inside the C allocator. */
    printf("TOP: DOK N03 MEMORY\n");
    final_heap_check();
    consoleClear();
    printf("TOP: DOK N03 MEMORY\n\n%s errors=%03lX tests=%03lX\n",
           observation.status == 1 ? "PASS" : "FAIL",
           (unsigned long)observation.error_bits, (unsigned long)observation.tests_pass_mask);
    printf("ARM9 RAM: %lu KiB\nStatic: %lu B\nManaged: %lu KiB\n",
           (unsigned long)(observation.region_bytes / 1024), (unsigned long)observation.static_bytes,
           (unsigned long)(observation.slab_bytes / 1024));
    printf("Reserve: %lu KiB (17.85%%)\nHeap grow headroom: %lu B\n",
           (unsigned long)(observation.reserve_bytes / 1024), (unsigned long)observation.sdk_heap_available);
    printf("Startup overhead: %lu B\n\nEmulator evidence only\nHardware check: N24\n",
           (unsigned long)observation.startup_heap_overhead);
    consoleSelect(&bottom_console);
    printf("BOTTOM: BOUNDS / HIGH-WATER\n\n7 arenas: full + reject + reset\n");
    printf("Touched %lu bytes\nWork stack: %lu / %lu\nRejected pushes: %lu\n",
           (unsigned long)observation.touched_bytes,
           (unsigned long)observation.work_stack_high_water,
           (unsigned long)observation.work_stack_capacity,
           (unsigned long)observation.work_stack_failures);
    printf("\nDTCM painted span only\nBefore: %lu B  After: %lu B\nGuard: %s\n",
           (unsigned long)observation.paint_watermark_before,
           (unsigned long)observation.paint_watermark_after,
           observation.paint_guard_ok ? "OK" : "FAIL");
    printf("\nNot a full-game RAM peak\nNo game assets loaded\n");
    final_heap_check();
    consoleSelect(&top_console);
    consoleSetCursor(&top_console, 0, 2);
    printf("%s errors=%03lX tests=%03lX\n",
           observation.status == 1 ? "PASS" : "FAIL",
           (unsigned long)observation.error_bits, (unsigned long)observation.tests_pass_mask);
    consoleSetCursor(&top_console, 0, 7);
    printf("Heap grow headroom: %lu B  \nStartup overhead: %lu B  \n",
           (unsigned long)observation.sdk_heap_available,
           (unsigned long)observation.startup_heap_overhead);
    for (;;) {
        ++observation.frame;
        publish();
        swiWaitForVBlank();
    }
}
