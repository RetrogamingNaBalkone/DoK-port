#ifndef NDS_VISUAL_PROBE_H
#define NDS_VISUAL_PROBE_H
#include <stdint.h>
#define NDS_VISUAL_MAGIC 0x36564b44u
typedef struct NdsVisualProbe {
    uint32_t magic, abi_version, size_bytes, sequence;
    uint32_t frame, status, error, ordinal, case_id, case_count;
    uint32_t top_base, bottom_base, top_bytes, bottom_bytes;
    uint32_t heap_limit_before, heap_limit_final, reserve_bytes, static_bytes, startup_overhead;
    uint32_t asset_pool_bytes, staging_bytes, sdk_heap_available;
    uint32_t read_calls, read_bytes, max_request, displayed;
} NdsVisualProbe;
#endif
