#ifndef NDS_MEMORY_PROBE_H
#define NDS_MEMORY_PROBE_H

#include <stdint.h>

#define NDS_MEMORY_MAGIC UINT32_C(0x334d4b44)
#define NDS_MEMORY_ABI UINT32_C(1)
#define NDS_MEMORY_ARENAS 7u

/* Read-only emulator observation ABI, little-endian uint32 fields only.
 * sequence is odd while publishing; read two equal even sequences.
 * Addresses are ARM9 virtual addresses, not file offsets.
 * status 0=starting,1=passed,2=failed. No tool may write this state.
 */
typedef struct NdsMemoryArenaProbe {
    uint32_t capacity;
    uint32_t used;
    uint32_t high_water;
    uint32_t failures;
} NdsMemoryArenaProbe;

typedef struct NdsMemoryProbe {
    uint32_t magic;
    uint32_t abi_version;
    uint32_t size_bytes;
    uint32_t sequence;
    uint32_t status;
    uint32_t error_bits;
    uint32_t frame;
    uint32_t tests_pass_mask;
    uint32_t is_dsi;
    uint32_t region_base;
    uint32_t region_limit;
    uint32_t region_bytes;
    uint32_t static_end;
    uint32_t static_bytes;
    uint32_t static_ceiling;
    uint32_t malloc_request_bytes;
    uint32_t malloc_raw;
    uint32_t slab_base;
    uint32_t slab_bytes;
    uint32_t heap_start;
    uint32_t heap_end_before;
    uint32_t heap_limit_before;
    uint32_t sdk_limit_exclusion_bytes;
    uint32_t heap_limit_policy;
    uint32_t heap_end_reduced;
    uint32_t heap_limit_reduced;
    uint32_t heap_end_allocated;
    uint32_t heap_end_final;
    uint32_t heap_limit_final;
    uint32_t reserve_bytes;
    uint32_t sdk_heap_available;
    uint32_t sdk_heap_min;
    uint32_t startup_heap_overhead;
    uint32_t static_and_overhead;
    uint32_t code_envelope_headroom;
    uint32_t touched_bytes;
    uint32_t itcm_base;
    uint32_t itcm_end;
    uint32_t itcm_used;
    uint32_t dtcm_base;
    uint32_t dtcm_top;
    uint32_t dtcm_sbss_end;
    uint32_t user_stack_top;
    uint32_t user_stack_bytes;
    uint32_t observed_sp;
    uint32_t paint_lo;
    uint32_t paint_hi;
    uint32_t paint_watermark_before;
    uint32_t paint_watermark_after;
    uint32_t paint_guard_ok;
    uint32_t cp15_dtcm;
    uint32_t cp15_itcm;
    uint32_t work_stack_capacity;
    uint32_t work_stack_depth;
    uint32_t work_stack_high_water;
    uint32_t work_stack_failures;
    uint32_t game_state_bytes;
    uint32_t game_lifecycle_generation;
    NdsMemoryArenaProbe arenas[NDS_MEMORY_ARENAS];
} NdsMemoryProbe;

_Static_assert(sizeof(NdsMemoryProbe) == 86u * sizeof(uint32_t), "N03 probe ABI");

#endif
