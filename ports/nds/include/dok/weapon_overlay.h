#ifndef DOK_WEAPON_OVERLAY_H
#define DOK_WEAPON_OVERLAY_H
#include "dok/combat.h"
/* Same exact source sampling as N15, bounded to its nonempty screen rectangle. */
dok_status dok_weapon_overlay_clipped(const dok_weapon_view *,const unsigned char *,uint16_t,uint32_t,unsigned char *,uint32_t);
/* Write the union of the old and current screen rows. A NULL previous view
 * initializes the whole overlay. The returned end row is exclusive. */
dok_status dok_weapon_overlay_rows(const dok_weapon_view *,const dok_weapon_view *,
    const unsigned char *,uint16_t,uint32_t,unsigned char *,uint32_t,uint32_t *,uint32_t *);
#endif
