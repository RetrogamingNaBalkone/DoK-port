#ifndef DOK_MEMORY_BUDGET_H
#define DOK_MEMORY_BUDGET_H

#include <stdint.h>

/* N03 accepted planning ceilings, not measured full-game consumption. */
#define DOK_NDS_MAIN_BASE UINT32_C(0x02000000)
#define DOK_NDS_ARM9_LIMIT UINT32_C(0x02380000)
#define DOK_NDS_MAIN_BYTES UINT32_C(4194304)
#define DOK_NDS_ARM9_BYTES UINT32_C(3670016)
#define DOK_NDS_SDK_ARM7_BYTES UINT32_C(524288)
#define DOK_NDS_STATIC_CEILING UINT32_C(524288)
#define DOK_NDS_SDK_HEAP_MIN UINT32_C(196608)
#define DOK_NDS_RESERVE_BYTES UINT32_C(655360)
#define DOK_NDS_WORLD_BYTES UINT32_C(393216)
/* N22: all 512 event owners (three slots each) occupy exactly 36 KiB.
 * Move their unused 28 KiB tail to decoded images; keep the slab, SDK heap
 * floor and emergency reserve unchanged. */
#define DOK_NDS_EVENTS_BYTES UINT32_C(36864)
#define DOK_NDS_SCRATCH_BYTES UINT32_C(131072)
#define DOK_NDS_SCENE_BYTES UINT32_C(262144)
#define DOK_NDS_ASSETS_BYTES UINT32_C(1077248)
#define DOK_NDS_AUDIO_BYTES UINT32_C(262144)
#define DOK_NDS_IO_BYTES UINT32_C(131072)
#define DOK_NDS_SLAB_BYTES UINT32_C(2293760)
#define DOK_NDS_SLAB_COUNT UINT32_C(7)

_Static_assert(DOK_NDS_MAIN_BYTES == DOK_NDS_ARM9_BYTES + DOK_NDS_SDK_ARM7_BYTES,
               "DS mode memory split");
_Static_assert(DOK_NDS_SLAB_BYTES == DOK_NDS_WORLD_BYTES + DOK_NDS_EVENTS_BYTES +
               DOK_NDS_SCRATCH_BYTES + DOK_NDS_SCENE_BYTES + DOK_NDS_ASSETS_BYTES +
               DOK_NDS_AUDIO_BYTES + DOK_NDS_IO_BYTES, "N03 slab total");
_Static_assert(DOK_NDS_ARM9_BYTES == DOK_NDS_STATIC_CEILING + DOK_NDS_SLAB_BYTES +
               DOK_NDS_SDK_HEAP_MIN + DOK_NDS_RESERVE_BYTES, "N03 budget total");
_Static_assert(DOK_NDS_RESERVE_BYTES >= DOK_NDS_ARM9_BYTES / 10u + 1u,
               "At least ten percent reserved");

#endif
