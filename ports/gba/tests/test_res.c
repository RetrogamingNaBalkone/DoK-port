#include "dok/res.h"
#include <stdio.h>
#include <string.h>
#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "line %d: %s\n", __LINE__, #expr); return 1; } } while (0)

int main(void) {
    /* Synthetic archive with dirty post-NUL name bytes and stale DOS cache state. */
    uint8_t data[66] = {2, 0};
    uint8_t changed[66];
    dok_res_archive a;
    dok_res_entry entry;
    const uint8_t *payload;
    size_t size;
    memcpy(data + 2, "FONT8", 6);
    data[10] = 0xff;
    data[18] = 3; data[22] = 62;
    memset(data + 26, 0xa5, 6);
    data[32] = 0x80; data[33] = 0x81; data[34] = 0;
    data[48] = 1; data[52] = 65;
    data[62] = 0x00; data[63] = 0x80; data[64] = 0xff; data[65] = 0x42;
    CHECK(dok_res_open(&a, data, sizeof(data)) == DOK_RES_OK);
    CHECK(a.count == 2);
    CHECK(dok_res_find(&a, (const uint8_t *)"FONT8", 5, &entry) == DOK_RES_OK);
    CHECK(entry.offset == 62 && entry.size == 3);
    CHECK(dok_res_find(&a, (const uint8_t *)"FONT", 4, &entry) == DOK_RES_NOT_FOUND);
    CHECK(dok_res_find(&a, (const uint8_t *)"font8", 5, &entry) == DOK_RES_NOT_FOUND);
    CHECK(dok_res_find(&a, data + 32, 2, &entry) == DOK_RES_OK && entry.offset == 65);
    CHECK(dok_res_payload(&a, 0, &payload, &size) == DOK_RES_OK);
    CHECK(size == 3 && payload[0] == 0 && payload[1] == 0x80 && payload[2] == 0xff);
    CHECK(dok_res_payload(&a, 2, &payload, &size) == DOK_RES_NOT_FOUND);
    CHECK(payload == NULL && size == 0);
    memcpy(changed, data, sizeof(data));
    memcpy(changed + 32, "FONT8", 6);
    CHECK(dok_res_open(&a, changed, sizeof(changed)) == DOK_RES_OK);
    CHECK(dok_res_find(&a, (const uint8_t *)"FONT8", 5, &entry) == DOK_RES_OK && entry.offset == 62);
    CHECK(dok_res_open(&a, data, 61) == DOK_RES_TRUNCATED && a.data == NULL);
    CHECK(dok_res_open(&a, data, 65) == DOK_RES_INVALID_RANGE && a.data == NULL);
    memcpy(changed, data, sizeof(data));
    memset(changed + 22, 0xff, 4);
    CHECK(dok_res_open(&a, changed, sizeof(changed)) == DOK_RES_INVALID_RANGE);
    memcpy(changed, data, sizeof(data));
    memset(changed + 18, 0xff, 4);
    CHECK(dok_res_open(&a, changed, sizeof(changed)) == DOK_RES_INVALID_RANGE);
    memcpy(changed, data, sizeof(data)); changed[22] = 1;
    CHECK(dok_res_open(&a, changed, sizeof(changed)) == DOK_RES_INVALID_RANGE);
    memcpy(changed, data, sizeof(data)); memset(changed + 2, 'A', 16);
    CHECK(dok_res_open(&a, changed, sizeof(changed)) == DOK_RES_INVALID_NAME);
    memcpy(changed, data, sizeof(data)); changed[2] = 0;
    CHECK(dok_res_open(&a, changed, sizeof(changed)) == DOK_RES_INVALID_NAME);
    CHECK(dok_res_open(&a, data, 1) == DOK_RES_TRUNCATED);
    CHECK(dok_res_open(&a, NULL, 0) == DOK_RES_INVALID_ARGUMENT);
    changed[0] = 0; changed[1] = 0;
    CHECK(dok_res_open(&a, changed, 2) == DOK_RES_OK && a.count == 0);
    puts("resource reader: synthetic bounds, names, duplicate order and cache isolation passed");
    return 0;
}
