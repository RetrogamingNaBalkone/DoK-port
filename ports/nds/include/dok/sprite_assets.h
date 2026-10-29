#ifndef DOK_SPRITE_ASSETS_H
#define DOK_SPRITE_ASSETS_H
#include "dok/sprites.h"

/* All native image/frame metadata is small and pinned, pixels are demanded
 * separately. No frame selection performs catalog I/O. */
#define DOK_SPRITE_IMAGES 256u
#define DOK_SPRITE_ANIMATIONS 24u
typedef struct dok_sprite_asset_bank {
    dok_sprite_binding bindings[256];
    dok_asset_entry images[DOK_SPRITE_IMAGES];
    dok_asset_id animations[DOK_SPRITE_ANIMATIONS];
    dok_sprite_animation sequences[DOK_SPRITE_ANIMATIONS];
    uint32_t image_count, animation_count, metadata_bytes;
} dok_sprite_asset_bank;
dok_status dok_sprite_assets_load(const dok_level_presentation *presentation,
    dok_sprite_asset_bank *bank,unsigned char *pool,uint32_t capacity);
/* O(256) bounded metadata lookup, no disk read or state mutation. */
const dok_asset_entry *dok_sprite_asset_image(const dok_sprite_asset_bank *bank,
    dok_asset_id id);
/* Snapshot binding change at simulation boundary; caller must not have an
 * outstanding scene/list lease. Native animation runtime flags are always8. */
dok_status dok_sprite_asset_select(dok_sprite_asset_bank *bank,uint8_t glyph,
    dok_asset_id animation,int32_t frame);
#endif
