/* Native loader: RUS 1050:026b / ENG 1018:11b1. See docs/originals/animations.md. */
#include "dok/animation.h"
#ifdef DOK_RENDERER_PROBE
/* G20: the build validates all frames of immutable, bound cartridge resources.
 * A matching pointer AND exact size may reuse that validation. Mutable buffers
 * and forged/mismatched views still take the ordinary checked decoder. */
extern const uint8_t dok_gba_assets[],dok_gba_assets_end[];
extern const uint32_t dok_animation_index[];
static const void *last_data;
static size_t last_size;
static const uint32_t *last_index;
static const uint32_t *indexed(const void *data,size_t size) {
    if(data==last_data && size==last_size) return last_index;
    uintptr_t address=(uintptr_t)data,base=(uintptr_t)dok_gba_assets;
    if(address<base || address>=(uintptr_t)dok_gba_assets_end) return 0;
    uint32_t offset=(uint32_t)(address-base),lo=0,hi=dok_animation_index[0];
    while(lo<hi) {
        unsigned mid=lo+(hi-lo)/2;
        const uint32_t *row=dok_animation_index+1+mid*4;
        if(row[0]==offset) {
            if(row[1]!=size) return 0;
            last_data=data;last_size=size;last_index=row;return row;
        }
        if(row[0]<offset) lo=mid+1;else hi=mid;
    }
    return 0;
}
#endif
static uint16_t u16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static int16_t s16(const uint8_t *p) {
    uint16_t n = u16(p);
    return n <= INT16_MAX ? (int16_t)n : (int16_t)(-1 - (int16_t)(UINT16_MAX - n));
}
dok_asset_status dok_animation_open(const void *data, size_t size, dok_animation_view *out) {
    const uint8_t *p = (const uint8_t *)data;
    dok_animation_view animation;
    size_t offset;
    uint32_t i;
    if (!data || !out) return DOK_ASSET_INVALID_ARGUMENT;
    if (size < 8u) return DOK_ASSET_TRUNCATED;
#ifdef DOK_RENDERER_PROBE
    const uint32_t *cached=indexed(data,size);
    if(cached) {
        animation.data=p;animation.size=size;animation.frame_count=(uint16_t)cached[2];
        animation.images_offset=8u+(size_t)animation.frame_count*10u;
        *out=animation;return DOK_ASSET_OK;
    }
#endif
    animation.frame_count = u16(p);
    /* The original loop uses a signed 16-bit count and assumes at least one frame. */
    if (!animation.frame_count || animation.frame_count > INT16_MAX) return DOK_ASSET_INVALID_FORMAT;
    animation.images_offset = 8u + (size_t)animation.frame_count * 10u;
    if (animation.images_offset > size) return DOK_ASSET_TRUNCATED;
    animation.data = p; animation.size = size;
    offset = animation.images_offset;
    for (i = 0; i < animation.frame_count; ++i) {
        dok_image_view image;
        dok_asset_status status = dok_image_read_prefix(p + offset, size - offset, &image);
        if (status != DOK_ASSET_OK) return status;
        offset += image.encoded_size;
    }
    if (offset != size) return DOK_ASSET_TRAILING_DATA;
    *out = animation;
    return DOK_ASSET_OK;
}
dok_asset_status dok_animation_begin(const dok_animation_view *a, dok_animation_iterator *out) {
    dok_animation_iterator iterator;
    if (!a || !out || !a->data || !a->frame_count || a->images_offset > a->size)
        return DOK_ASSET_INVALID_ARGUMENT;
    iterator.animation = a; iterator.next_index = 0; iterator.image_offset = a->images_offset;
    *out = iterator;
    return DOK_ASSET_OK;
}
dok_asset_status dok_animation_next(dok_animation_iterator *it, dok_animation_frame *out) {
    const dok_animation_view *a;
    const uint8_t *record;
    dok_animation_frame frame;
    dok_asset_status status;
    if (!it || !out || !it->animation) return DOK_ASSET_INVALID_ARGUMENT;
    a = it->animation;
    if (!a->data || it->image_offset > a->size) return DOK_ASSET_INVALID_ARGUMENT;
    if (it->next_index >= a->frame_count) return DOK_ASSET_END;
    if (8u + ((size_t)it->next_index + 1u) * 10u > a->size) return DOK_ASSET_TRUNCATED;
    record = a->data + 8u + (size_t)it->next_index * 10u;
    frame.index = it->next_index; frame.interval_bits = u16(record);
    frame.interval_ticks = s16(record); frame.parameter_a = s16(record + 2); frame.parameter_b = s16(record + 4);
    frame.image_offset = it->image_offset;
    status = dok_image_read_prefix(a->data + it->image_offset, a->size - it->image_offset, &frame.image);
    if (status != DOK_ASSET_OK) return status;
    it->image_offset += frame.image.encoded_size; ++it->next_index;
    *out = frame;
    return DOK_ASSET_OK;
}
dok_asset_status dok_animation_frame_at(const dok_animation_view *a, uint16_t index,
                                      dok_animation_frame *out) {
    dok_animation_iterator it;
    dok_animation_frame frame;
    dok_asset_status status;
    if (!a || !out) return DOK_ASSET_INVALID_ARGUMENT;
    if (index >= a->frame_count) return DOK_ASSET_END;
#ifdef DOK_RENDERER_PROBE
    const uint32_t *cached=indexed(a->data,a->size);
    if(cached && a->frame_count==cached[2] && a->images_offset==8u+(size_t)a->frame_count*10u) {
        it.animation=a;it.next_index=index;
        it.image_offset=dok_animation_index[cached[3]+index];
        return dok_animation_next(&it,out);
    }
#endif
    status = dok_animation_begin(a, &it);
    if (status != DOK_ASSET_OK) return status;
    do {
        status = dok_animation_next(&it, &frame);
        if (status != DOK_ASSET_OK) return status;
    } while (frame.index != index);
    *out = frame;
    return DOK_ASSET_OK;
}
