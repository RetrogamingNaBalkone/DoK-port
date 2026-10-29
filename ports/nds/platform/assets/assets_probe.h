#ifndef NDS_ASSETS_PROBE_H
#define NDS_ASSETS_PROBE_H
#include <stdint.h>
#define NDS_ASSETS_MAGIC UINT32_C(0x34414b44)
#define NDS_ASSETS_ABI 1u
#define NDS_ASSETS_PASS_MASK UINT32_C(0x3fff)
/* Read-only little-endian observation ABI. Two equal even sequence values
 * surround a coherent publication. Never inject state through this object.
 * Status: 0=running, 1=passed, 2=failed, 3=no FAT media, 4=package missing. */
typedef struct NdsAssetsProbe {
    uint32_t magic,abi_version,size_bytes,sequence;
    uint32_t status,error_bits,frame,tests_pass_mask;
    uint32_t open_status,type_mask,entry_count,raw_count;
    uint32_t file_bytes,read_calls,read_bytes,max_request;
    uint32_t asset_pool_bytes,asset_highwater,workspace_bytes,pinned_refusal;
    uint32_t generation,long_sample_count,long_chunks,long_read_samples;
    uint32_t largest_gpu_bytes,negative_pass_mask,heap_limit_before,heap_limit_final;
    uint32_t reserve_bytes,sdk_heap_available,static_bytes,case_id;
    uint32_t asset_pool_base,cached_base,cached_id,cached_bytes;
} NdsAssetsProbe;
_Static_assert(sizeof(NdsAssetsProbe)==144u,"N04 read-only ABI");
#endif
