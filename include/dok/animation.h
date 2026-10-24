#ifndef DOK_ANIMATION_H
#define DOK_ANIMATION_H
#include "dok/image.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Keep both the view and its input bytes alive and unchanged during iteration. */
typedef struct dok_animation_view {
    const uint8_t *data;
    size_t size, images_offset;
    uint16_t frame_count;
} dok_animation_view;
typedef struct dok_animation_frame {
    uint16_t index, interval_bits;
    int16_t interval_ticks, parameter_a, parameter_b;
    size_t image_offset;
    dok_image_view image;
} dok_animation_frame;
typedef struct dok_animation_iterator {
    const dok_animation_view *animation;
    size_t image_offset;
    uint16_t next_index;
} dok_animation_iterator;
/* Allocation-free full validation. Outputs stay untouched on failure. */
dok_asset_status dok_animation_open(const void *data, size_t size, dok_animation_view *out);
dok_asset_status dok_animation_begin(const dok_animation_view *animation, dok_animation_iterator *out);
dok_asset_status dok_animation_next(dok_animation_iterator *iterator, dok_animation_frame *out);
/* O(index). For sequential playback/export use an iterator instead. */
dok_asset_status dok_animation_frame_at(const dok_animation_view *animation, uint16_t index,
                                      dok_animation_frame *out);
#ifdef __cplusplus
}
#endif
#endif
