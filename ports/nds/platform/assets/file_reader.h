#ifndef NDS_ASSET_FILE_READER_H
#define NDS_ASSET_FILE_READER_H
#include "dok/assets.h"
#include <stdio.h>
typedef struct NdsAssetFile {
    FILE *file;
    uint32_t bytes,calls,bytes_read,max_request;
    /* Probe-only callback fault injection. No package/state/RAM injection. */
    uint32_t test_fault_offset,test_fault_mode;
    unsigned char *catalog_cache;
    uint32_t catalog_end,cache_tags[8],cache_hits,storage_reads,position;
} NdsAssetFile;
dok_asset_status nds_asset_file_open(NdsAssetFile *file,const char *path,dok_asset_reader *out);
void nds_asset_file_close(NdsAssetFile *file);
/* Reuses a caller-owned post-open 32KiB region for immutable catalog pages. */
void nds_asset_file_cache(NdsAssetFile *,unsigned char *,uint32_t entries);
#endif
