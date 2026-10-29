#ifndef NDS_WORLD_PROBE_H
#define NDS_WORLD_PROBE_H
#include <stdint.h>
#define NDS_WORLD_MAGIC 0x35574B44u
#define NDS_WORLD_ABI 1u
typedef struct NdsWorldProbe {
    uint32_t magic, abi_version, size_bytes, sequence;
    uint32_t status, error_bits, frame, case_id, case_count;
    uint32_t level, language, tests_pass_mask, negative_pass_mask;
    uint32_t json_base, json_bytes, world_base, world_bytes, scratch_bytes;
    uint32_t asset_pool_bytes, io_bytes, heap_limit_before, heap_limit_final;
    uint32_t sdk_heap_available, reserve_bytes, static_bytes;
    uint32_t entry_count, read_calls, read_bytes, max_request;
    uint32_t changes, retained_assertions, boyars, mice;
    uint32_t actor_placements, placed_actors;
    uint32_t startup_overhead, allocated_slabs;
} NdsWorldProbe;
#endif
