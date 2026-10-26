#ifndef DOK_ITEMS_H
#define DOK_ITEMS_H
#include "dok/runtime.h"
enum { DOK_STAT_HEALTH=0,DOK_STAT_CHARGE=1,DOK_STAT_HEALTH_MAX=2,
 DOK_STAT_CHARGE_MAX=3,DOK_STAT_KEYS=10,DOK_STAT_SCORE=99,DOK_STAT_MAP=200 };
typedef enum {DOK_ITEM_STAT=1,DOK_ITEM_PICKUP=2,DOK_ITEM_WEAPON=3,DOK_ITEM_MAP=4} dok_item_kind;
/* Values are synchronous scalar notifications. PICKUP identifies the definition
 * whose raw CP866 message(+58), sound and marker a UI/audio consumer may resolve.
 * No pointer to a transient message, no rendering or sound from game rules. */
typedef void (*dok_item_sink)(void *,dok_item_kind,uint16_t,int16_t,int16_t);
typedef struct {dok_runtime *world;dok_item_sink sink;void *context;} dok_items;
void dok_items_init(dok_items *,dok_runtime *,dok_item_sink,void *);
/* Exact signed16 ADD then signed clamp; max changes wrap and refill. Unsupported
 * categories return0 without writing arbitrary native memory. */
int dok_items_stat(dok_items *,int16_t category,int16_t value);
/* Scan player's current TMap cell, independently of MMap occupancy. Returns1
 * when accepted (even if persistent); full positive health/charge returns0. */
int dok_items_pickup(dok_items *);
#endif
