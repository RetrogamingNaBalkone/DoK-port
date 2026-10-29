#include "dok/assets.h"
#include "dok/sha256.h"
_Static_assert(DOK_ASSET_READ_MAX==65536u,"bounded reads");
_Static_assert(sizeof(dok_asset_workspace)<=DOK_NDS_IO_BYTES,"I/O budget");
_Static_assert(DOK_ASSET_TEXT_CP866==16,"v1 representations");
int dok_nds_assets_compile_gate(void)
{
    dok_asset_catalog catalog={1u,1u,0};
    return catalog.implementation==0?0:1;
}
