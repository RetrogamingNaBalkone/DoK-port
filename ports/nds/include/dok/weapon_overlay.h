#ifndef DOK_WEAPON_OVERLAY_H
#define DOK_WEAPON_OVERLAY_H
#include "dok/combat.h"
/* Same exact source sampling as N15, bounded to its nonempty screen rectangle. */
dok_status dok_weapon_overlay_clipped(const dok_weapon_view *,const unsigned char *,uint16_t,uint32_t,unsigned char *,uint32_t);
#endif
