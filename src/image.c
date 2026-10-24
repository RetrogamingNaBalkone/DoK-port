#include "dok/image.h"
static uint16_t u16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
dok_asset_status dok_image_read_prefix(const void *data, size_t size, dok_image_view *out) {
    const uint8_t *p = (const uint8_t *)data;
    dok_image_view image;
    if (!data || !out) return DOK_ASSET_INVALID_ARGUMENT;
    if (size < 18u) return DOK_ASSET_TRUNCATED;
    image.width = u16(p); image.height = u16(p + 2);
    image.stored_flags = u16(p + 4);
    image.crop_divisor = u16(p + 6); image.crop_index = u16(p + 8);
    image.pixel_bytes = u32(p + 10);
    if (!image.width || !image.height || image.pixel_bytes != (uint32_t)image.width * image.height)
        return DOK_ASSET_INVALID_FORMAT;
    if ((size_t)image.pixel_bytes > size - 18u) return DOK_ASSET_TRUNCATED;
    image.pixels = p + 18; image.encoded_size = 18u + (size_t)image.pixel_bytes;
    *out = image;
    return DOK_ASSET_OK;
}
dok_asset_status dok_image_open(const void *data, size_t size, dok_image_view *out) {
    dok_image_view image;
    dok_asset_status status;
    if (!out) return DOK_ASSET_INVALID_ARGUMENT;
    status = dok_image_read_prefix(data, size, &image);
    if (status != DOK_ASSET_OK) return status;
    if (image.encoded_size != size) return DOK_ASSET_TRAILING_DATA;
    *out = image;
    return DOK_ASSET_OK;
}
const char *dok_asset_status_string(dok_asset_status s) {
    switch (s) {
    case DOK_ASSET_OK: return "ok";
    case DOK_ASSET_INVALID_ARGUMENT: return "invalid_argument";
    case DOK_ASSET_TRUNCATED: return "truncated";
    case DOK_ASSET_INVALID_FORMAT: return "invalid_format";
    case DOK_ASSET_TRAILING_DATA: return "trailing_data";
    case DOK_ASSET_END: return "end";
    default: return "unknown_status";
    }
}
