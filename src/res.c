/* Recovered container semantics: PKR 10e0:0000/0251/046b; PKE 1058:0c02/0e6b/1085.
 * See docs/resources.md. Explicit LE reads avoid host alignment and 16-bit ABI assumptions.
 */
#include "dok/res.h"
#include <string.h>

static uint16_t le16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
dok_res_status dok_res_entry_at(const dok_res_archive *a, uint16_t index,
                              dok_res_entry *out) {
    const uint8_t *p;
    size_t directory_end, length = 0;
    dok_res_entry entry;
    if (!a || !out || !a->data) return DOK_RES_INVALID_ARGUMENT;
    if (index >= a->count) return DOK_RES_NOT_FOUND;
    directory_end = 2u + (size_t)a->count * 30u;
    if (directory_end > a->size) return DOK_RES_TRUNCATED;
    p = a->data + 2u + (size_t)index * 30u;
    while (length < 16u && p[length]) ++length;
    if (!length || length == 16u) return DOK_RES_INVALID_NAME;
    memset(&entry, 0, sizeof(entry));
    memcpy(entry.name, p, length);
    entry.name_length = (uint8_t)length;
    entry.size = le32(p + 16);
    entry.offset = le32(p + 20);
    if ((size_t)entry.offset < directory_end || (size_t)entry.offset > a->size ||
        (size_t)entry.size > a->size - (size_t)entry.offset) return DOK_RES_INVALID_RANGE;
    *out = entry;
    return DOK_RES_OK;
}
dok_res_status dok_res_open(dok_res_archive *out, const void *data, size_t size) {
    dok_res_archive candidate;
    uint32_t i;
    if (!out) return DOK_RES_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    if (!data) return DOK_RES_INVALID_ARGUMENT;
    if (size < 2u) return DOK_RES_TRUNCATED;
    candidate.data = (const uint8_t *)data;
    candidate.size = size;
    candidate.count = le16(candidate.data);
    if (2u + (size_t)candidate.count * 30u > size) return DOK_RES_TRUNCATED;
    for (i = 0; i < candidate.count; ++i) {
        dok_res_entry entry;
        dok_res_status status = dok_res_entry_at(&candidate, (uint16_t)i, &entry);
        if (status != DOK_RES_OK) return status;
    }
    *out = candidate;
    return DOK_RES_OK;
}
dok_res_status dok_res_find(const dok_res_archive *a, const uint8_t *name,
                          size_t name_length, dok_res_entry *out) {
    uint32_t i;
    if (!a || !a->data || !out || !name || !name_length || name_length >= 16u)
        return DOK_RES_INVALID_ARGUMENT;
    for (i = 0; i < a->count; ++i) {
        dok_res_entry entry;
        dok_res_status status = dok_res_entry_at(a, (uint16_t)i, &entry);
        if (status != DOK_RES_OK) return status;
        if (entry.name_length == name_length && !memcmp(entry.name, name, name_length)) {
            *out = entry;
            return DOK_RES_OK;
        }
    }
    return DOK_RES_NOT_FOUND;
}
dok_res_status dok_res_payload(const dok_res_archive *a, uint16_t index,
                             const uint8_t **data, size_t *size) {
    dok_res_entry entry;
    dok_res_status status;
    if (!data || !size) return DOK_RES_INVALID_ARGUMENT;
    *data = NULL;
    *size = 0;
    status = dok_res_entry_at(a, index, &entry);
    if (status != DOK_RES_OK) return status;
    *data = a->data + entry.offset;
    *size = entry.size;
    return DOK_RES_OK;
}
const char *dok_res_status_string(dok_res_status s) {
    switch (s) {
    case DOK_RES_OK: return "ok";
    case DOK_RES_INVALID_ARGUMENT: return "invalid_argument";
    case DOK_RES_TRUNCATED: return "truncated";
    case DOK_RES_INVALID_NAME: return "invalid_name";
    case DOK_RES_INVALID_RANGE: return "invalid_range";
    case DOK_RES_NOT_FOUND: return "not_found";
    default: return "unknown_status";
    }
}
