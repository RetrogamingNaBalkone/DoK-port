#ifndef DOK_NDS_FRONTEND_VISUAL_H
#define DOK_NDS_FRONTEND_VISUAL_H
#include "dok/frame_store.h"

/* Rendering-only localization. Never replace the simulation sprite bank or
 * animation IDs when the UI language changes. Only POPAL frame5 differs. */
typedef struct dok_frontend_visual {
    dok_asset_entry entries[2];
    uint32_t tag;
} dok_frontend_visual;

dok_status dok_frontend_visual_init(dok_frontend_visual *,
                                    const dok_asset_session *);
const dok_asset_entry *dok_frontend_visual_resolve(const dok_frontend_visual *,
    const dok_asset_entry *source,uint8_t language);
/* Append missing localized frames after dok_frame_store_load and BEFORE the
 * remaining slab is assigned to the raw cache. Existing records/prefix bytes
 * survive failure. Unpublished tail/scratch bytes may change; metadata only
 * commits after both SHA/schema-checked loads and pack/unpack round trips.
 * Pool must be store->pool; scratch is separate live storage. No borrowers or
 * cache may refer to a prospective tail partition during this operation. */
dok_status dok_frontend_visual_append_store(const dok_frontend_visual *,
    dok_frame_store *,const dok_asset_session *,unsigned char *pool,
    uint32_t capacity,unsigned char *scratch,uint32_t scratch_bytes);
#endif
