#ifndef DOK_RES_H
#define DOK_RES_H
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Immutable borrowed view. The caller keeps data alive and unchanged. No allocation. */
typedef struct dok_res_archive {
    const uint8_t *data;
    size_t size;
    uint16_t count;
} dok_res_archive;

typedef struct dok_res_entry {
    uint8_t name[16]; /* Original single-byte name; bytes after NUL are ignored. */
    uint8_t name_length;
    uint32_t size;
    uint32_t offset;
} dok_res_entry;

typedef enum dok_res_status {
    DOK_RES_OK = 0, DOK_RES_INVALID_ARGUMENT, DOK_RES_TRUNCATED,
    DOK_RES_INVALID_NAME, DOK_RES_INVALID_RANGE, DOK_RES_NOT_FOUND
} dok_res_status;

dok_res_status dok_res_open(dok_res_archive *out, const void *data, size_t size);
dok_res_status dok_res_entry_at(const dok_res_archive *archive, uint16_t index,
                              dok_res_entry *out);
/* Byte-exact, case-sensitive lookup; first duplicate wins, as in the original. */
dok_res_status dok_res_find(const dok_res_archive *archive, const uint8_t *name,
                          size_t name_length, dok_res_entry *out);
dok_res_status dok_res_payload(const dok_res_archive *archive, uint16_t index,
                             const uint8_t **data, size_t *size);
const char *dok_res_status_string(dok_res_status status);

#ifdef __cplusplus
}
#endif
#endif
