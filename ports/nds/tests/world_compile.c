#include "dok/world.h"
#include "dok/balance.h"
#include "world_export.h"
_Static_assert(sizeof(dok_world_workspace)<=DOK_NDS_SCRATCH_BYTES,"scratch budget");
_Static_assert(3u*sizeof(dok_level_definition)+sizeof(dok_initial_world)+
    sizeof(dok_level_presentation)+sizeof(dok_balance_diff)<=DOK_NDS_WORLD_BYTES,
    "three borrowed level models plus seed and presentation fit world");
int dok_n05_compile_gate(void) { return DOK_BALANCE_CHANGE_COUNT == 39u; }
