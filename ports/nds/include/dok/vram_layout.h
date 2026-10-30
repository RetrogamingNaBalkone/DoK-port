#ifndef DOK_NDS_VRAM_LAYOUT_H
#define DOK_NDS_VRAM_LAYOUT_H
#include <stdint.h>
enum { DOK_VRAM_LAYOUT_SLOTS=128, DOK_VRAM_LAYOUT_MASK=16 };
typedef struct dok_vram_span {
    uint32_t address,bytes,upload_cost,slot;
} dok_vram_span;
typedef struct dok_vram_relocation {
    uint32_t address,bytes,cost,count;
    unsigned char evict[DOK_VRAM_LAYOUT_MASK];
} dok_vram_relocation;
/* Physical LCD VRAM pools A+B and D; C belongs to the lower screen. Inputs
 * are current SDK allocations and every still-missing request. No heap,
 * SDK calls, payload writes, or mutation on refusal. The selected window
 * also admits all evicted/missing sizes under largest-first/first-fit packing. */
int dok_vram_relocation_plan(const dok_vram_span *,uint32_t count,
    const uint32_t *missing,uint32_t missing_count,dok_vram_relocation *out);
#endif
