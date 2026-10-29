#ifndef NDS_ASSET_FILE_READER_H
#define NDS_ASSET_FILE_READER_H
#include "dok/assets.h"
#include <stdio.h>
typedef struct NdsAssetFile {
    FILE *file;
    uint32_t bytes,calls,bytes_read,max_request;
    /* Probe-only callback fault injection. No package/state/RAM injection. */
    uint32_t test_fault_offset,test_fault_mode;
} NdsAssetFile;
dok_asset_status nds_asset_file_open(NdsAssetFile *file,const char *path,dok_asset_reader *out);
void nds_asset_file_close(NdsAssetFile *file);
#endif
