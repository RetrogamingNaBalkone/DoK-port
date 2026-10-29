#ifndef DOK_SPRITES_PROBE_H
#define DOK_SPRITES_PROBE_H
#include <stdint.h>
typedef struct NdsSpriteProbe {
    uint32_t magic, abi_version, size_bytes, sequence;
    uint32_t list_address, list_bytes, sprite_count, considered, rejected;
    uint32_t bank_address, bank_bytes, binding_bytes, image_count, animation_count;
    uint32_t demand_address, demand_count, demand_bytes, cache_bytes;
    uint32_t gpu_hits, gpu_misses, gpu_evictions, gpu_rebuilds;
    uint32_t cpu_hits, cpu_misses, cpu_evictions, cpu_reads;
    uint32_t commit_start_line, commit_end_line, language, frame_fixture;
    uint32_t trace_address, trace_count, trace_bytes, trace_capacity;
} NdsSpriteProbe;
typedef struct NdsSpriteTrace {
    uint32_t image_id, frame_id, case_id, width, height, bytes;
    uint32_t hash0, hash1, hash2, hash3, hash4, hash5, hash6, hash7;
} NdsSpriteTrace;
#define NDS_SPRITE_TRACE_CAPACITY 512u
extern volatile NdsSpriteProbe g_nds_sprite_probe;
#endif
