#ifndef DOK_NDS_ITEMS_H
#define DOK_NDS_ITEMS_H
#include "dok/collision.h"

#define DOK_WEAPON_SLOTS 9u
enum { DOK_ITEM_STAT=1, DOK_ITEM_PICKUP=2, DOK_ITEM_WEAPON=3, DOK_ITEM_MAP=4 };
typedef struct dok_item_output {
    dok_object_ref object;
    uint32_t kind, cell;
    int16_t category, value;
    uint16_t slot, reserved;
} dok_item_output;
/* Ordered synchronous notifications. No reentry, mutation or retained pointer.
 * PICKUP references the RU definition; UI resolves its localized +58 text and
 * sound through the presentation catalog. No localized bytes in mutable state. */
typedef void (*dok_item_sink_fn)(void *,const dok_item_output *);
typedef struct dok_inventory {
    int16_t health,charge,max_health,max_charge;
    int16_t native_66_70[6],native_76_7a[3],score;
    uint16_t keys,map_available,selected,pending;
    dok_object_ref weapons[DOK_WEAPON_SLOTS];
} dok_inventory;
typedef struct dok_items {
    dok_initial_world *world;
    dok_collision_state *collision;
    dok_inventory inventory;
    dok_item_sink_fn sink;
    void *sink_context;
    uint32_t epoch,revision,busy,tag;
} dok_items;

/* Bind once to caller-owned storage outside dispatch/collision/render leases.
 * Initial stats come from the class1 definition, inventory starts empty. */
dok_status dok_items_bind(dok_items *,dok_initial_world *,dok_collision_state *,
    dok_item_sink_fn,void *);
/* Exact WORD arithmetic for defined stats. Unknown categories are refused;
 * 100/101/102/1000 belong to N11, never fallback pointer arithmetic. */
dok_status dok_items_stat(dok_items *,int16_t category,int16_t value,uint32_t *accepted);
/* Original pre-motion TMap cell scan, before N11 floor/hero movement. Removes
 * only the accepted cell, never shared definitions/animation/flags. */
dok_status dok_items_pickup(dok_items *,const dok_motion_state *,uint32_t *accepted);
/* Bounded cyclic selection. Zero/one slot is a no-op. Selection completes in
 * N15 hide/show, pending is the request; no array read precedes validation. */
dok_status dok_items_next_weapon(dok_items *,uint32_t *changed);
#endif
