#include "dok/res.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t fnv1a(const uint8_t *data, size_t size) {
    uint32_t hash = UINT32_C(2166136261);
    size_t i;
    for (i = 0; i < size; ++i) hash = (hash ^ data[i]) * UINT32_C(16777619);
    return hash;
}
int main(int argc, char **argv) {
    FILE *file;
    long length;
    uint8_t *bytes;
    dok_res_archive archive;
    dok_res_status status;
    uint32_t i;
    int result = 0;
    if (argc != 3 || strcmp(argv[1], "--list")) {
        fprintf(stderr, "Usage: dok-res --list ARCHIVE.RES\n"); return 2;
    }
    file = fopen(argv[2], "rb");
    if (!file) { perror(argv[2]); return 2; }
    if (fseek(file, 0, SEEK_END) || (length = ftell(file)) < 0 || fseek(file, 0, SEEK_SET)) {
        fclose(file); return 2;
    }
    bytes = (uint8_t *)malloc(length ? (size_t)length : 1u);
    if (!bytes) { fclose(file); return 2; }
    if (fread(bytes, 1, (size_t)length, file) != (size_t)length || ferror(file)) {
        fclose(file); free(bytes); return 2;
    }
    fclose(file);
    status = dok_res_open(&archive, bytes, (size_t)length);
    if (status != DOK_RES_OK) {
        fprintf(stderr, "%s\n", dok_res_status_string(status)); free(bytes); return 1;
    }
    puts("index\tname_hex\toffset\tsize\tfnv1a32");
    for (i = 0; i < archive.count; ++i) {
        dok_res_entry entry;
        const uint8_t *payload;
        size_t size, j;
        if (dok_res_entry_at(&archive, (uint16_t)i, &entry) != DOK_RES_OK ||
            dok_res_payload(&archive, (uint16_t)i, &payload, &size) != DOK_RES_OK) {
            result = 1; break;
        }
        printf("%u\t", (unsigned)i);
        for (j = 0; j < entry.name_length; ++j) printf("%02x", (unsigned)entry.name[j]);
        printf("\t%lu\t%lu\t%08lx\n", (unsigned long)entry.offset,
               (unsigned long)entry.size, (unsigned long)fnv1a(payload, size));
    }
    free(bytes);
    return result;
}
