/* Standalone public-header gate. These are native C types, not serialized ABI. */
#include "dok/contracts.h"
#include "dok/memory.h"
#include "dok/memory_budget.h"
#include "dok/contracts.h"
#include "dok/memory.h"
#include "dok/memory_budget.h"

#include <limits.h>

_Static_assert(CHAR_BIT == 8, "Byte-oriented asset and input interfaces require 8-bit bytes");
_Static_assert(sizeof(dok_asset_id) == sizeof(uint32_t), "Asset IDs are fixed-width values");
_Static_assert(sizeof(dok_object_id) == sizeof(uint32_t), "Object IDs are fixed-width values");
_Static_assert(sizeof(dok_world_epoch) == sizeof(uint32_t), "World epochs are fixed-width values");
_Static_assert(_Generic(((dok_input_frame *)0)->sequence, uint64_t: 1, default: 0),
               "Platform sample sequence is not a native-size counter");
_Static_assert(_Generic(((dok_object_ref *)0)->generation, uint32_t: 1, default: 0),
               "Object generation is a fixed-width value");

/* Sizes deliberately have no fixed expected values across host/ARM9 ABIs. */
const uint32_t dok_contracts_native_sizes[] = {
    sizeof(dok_platform_services), sizeof(dok_input_frame), sizeof(dok_game_state),
    sizeof(dok_scene_snapshot), sizeof(dok_asset_catalog), sizeof(dok_balance_profile),
    sizeof(dok_object_ref), sizeof(dok_arena), sizeof(dok_work_stack)
};
