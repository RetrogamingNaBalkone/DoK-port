#ifndef DOK_NDS_EFFECT_WORKSET_H
#define DOK_NDS_EFFECT_WORKSET_H
#include "dok/render_materials.h"
typedef struct dok_effect_workset_record {
    uint64_t asset_id;
    uint32_t material,flags,code,bytes;
} dok_effect_workset_record;
/* Bound for one ordinary GPU-fog presentation: LIGHT masks are zero; each
 * material has one shared phase/flags slot. Not an alpha/reference bound.
 * Mirrors effects_load's last class12 writer and effects_prepare's priority.
 * Optional records contain every water material, including code0/no payload. */
dok_status dok_effect_workset_bound(const dok_render_world *,uint32_t *,
    dok_effect_workset_record [DOK_RENDER_MATERIALS],uint32_t *);
#endif
