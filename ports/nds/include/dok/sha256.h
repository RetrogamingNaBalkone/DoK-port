#ifndef DOK_NDS_SHA256_H
#define DOK_NDS_SHA256_H
#include <stdint.h>
#include <stddef.h>
typedef struct dok_sha256 {
    uint32_t state[8];
    uint64_t bytes;
    uint32_t used;
    unsigned char block[64];
} dok_sha256;
void dok_sha256_begin(dok_sha256 *hash);
void dok_sha256_update(dok_sha256 *hash, const void *bytes, size_t count);
void dok_sha256_end(dok_sha256 *hash, unsigned char output[32]);
#endif
