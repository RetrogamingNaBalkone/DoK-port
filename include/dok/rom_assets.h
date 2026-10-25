#ifndef DOK_ROM_ASSETS_H
#define DOK_ROM_ASSETS_H
#include "dok/runtime.h"
#define DOK_ROM_VERSION 1u
#define DOK_ROM_HEADER_SIZE 64u
#define DOK_ROM_VIEW_SIZE 80u
#define DOK_ROM_RESOURCE_SIZE 32u
#define DOK_ROM_BINDING_SIZE 8u
#define DOK_ROM_LANGUAGE_RU 0u
#define DOK_ROM_LANGUAGE_EN 1u
#define DOK_ROM_SLOT_SPRITE 0xfff0u
#define DOK_ROM_SLOT_ANIMATION 0xfff1u
#define DOK_ROM_SLOT_SOUND 0xfff2u
#define DOK_ROM_SLOT_PALETTE 0xfff3u
#define DOK_ROM_SLOT_LIGHT 0xfff4u
#define DOK_ROM_GLOBAL_OBJECT 0xffffu
/* Version1 is an explicit LE,4-byte-aligned format. Header/view/resource/binding
 * records are decoded bytewise; pointer-free runtime records use asserted LE
 * layouts (32/8/36/64/60). No host pointers are serialized. See gba-assets.md. */
typedef struct {
    const uint8_t *data;
    size_t size;
    uint32_t resource_offset, view_offset, resource_count;
    uint16_t level_count, language_count;
} dok_rom_assets;
typedef struct {
    const uint8_t *data;
    uint32_t size;
    uint16_t id;
} dok_rom_resource;
typedef enum {
    DOK_ROM_OK, DOK_ROM_ARGUMENT, DOK_ROM_VERSION_ERROR, DOK_ROM_RANGE,
    DOK_ROM_ALIGNMENT, DOK_ROM_FORMAT, DOK_ROM_REFERENCE, DOK_ROM_NOT_FOUND,
    DOK_ROM_LANGUAGE_ERROR, DOK_ROM_PROFILE
} dok_rom_status;
/* Output remains untouched on failure. Caller keeps ROM immutable and alive.
 * Full validation includes every view and every resource link, no allocation.
 * The supplied base must be aligned4, including on machines allowing unaligned
 * reads. CRC/SHA are verified offline; runtime verifies structure and links. */
dok_rom_status dok_rom_open(dok_rom_assets *out,const void *data,size_t size);
/* level is zero-based0..2; language RU0/EN1. The small caller-owned view borrows
 * all arrays from ROM; no LEVEL parsing or RES/archive copy occurs. */
dok_rom_status dok_rom_level(dok_runtime_level *out,const dok_rom_assets *assets,unsigned level,unsigned language);
/* Stable IDs are sorted CP866 resource-name index+1.0 is invalid. Resource
 * payloads retain the already verified original image/animation/sound formats. */
dok_rom_status dok_rom_resource_at(dok_rom_resource *out,const dok_rom_assets *assets,uint16_t id,unsigned language);
dok_rom_status dok_rom_find(uint16_t *out,const dok_rom_assets *assets,const uint8_t *name,size_t size);
/* Binding slots use native offsets for class resource fields; common/global
 * slots are named above. Bindings are resolved offline to stable resource IDs. */
dok_rom_status dok_rom_binding(uint16_t *out,const dok_rom_assets *assets,unsigned level,unsigned language,
    uint16_t object_index,uint16_t slot);
/* view must be the exact caller-owned view borrowed by runtime. Replace its
 * localized descriptor/text/resource view only; preserve all mutable state.
 * Shared initial arrays/maps are required to enforce the common profile. */
dok_rom_status dok_rom_switch_language(dok_runtime *runtime,dok_runtime_level *view,
    const dok_rom_assets *assets,unsigned language);
#endif
