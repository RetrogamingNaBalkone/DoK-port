#ifndef DOK_IMAGE_H
#define DOK_IMAGE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum dok_asset_status {
    DOK_ASSET_OK = 0, DOK_ASSET_INVALID_ARGUMENT, DOK_ASSET_TRUNCATED,
    DOK_ASSET_INVALID_FORMAT, DOK_ASSET_TRAILING_DATA, DOK_ASSET_END
} dok_asset_status;
/* Borrowed, immutable indexed pixels. Serialized pointer bytes are never used. */
typedef struct dok_image_view {
    const uint8_t *pixels;
    uint16_t width, height, stored_flags, crop_divisor, crop_index;
    uint32_t pixel_bytes;
    size_t encoded_size;
} dok_image_view;
/* Prefix parser permits following images; open requires an exact standalone image. */
dok_asset_status dok_image_read_prefix(const void *data, size_t size, dok_image_view *out);
dok_asset_status dok_image_open(const void *data, size_t size, dok_image_view *out);
const char *dok_asset_status_string(dok_asset_status status);
#ifdef __cplusplus
}
#endif
#endif
