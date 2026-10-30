#ifndef STREAM_CLEAN_EQUIVALENCE_H
#define STREAM_CLEAN_EQUIVALENCE_H
#include "render_backend.h"
typedef struct stream_result {
    unsigned char texture[128][64],palette[8192],background[512];
    uint32_t pending[128],palette_pending,planned,result,dmas,flushes,fault;
    uint32_t prepare_result,prepare_state_unchanged;
    dok_nds_stream_stats stats;
} stream_result;
#endif
