#ifndef DOK_COMBAT_CASES_H
#define DOK_COMBAT_CASES_H
#include "dok/combat.h"
#include "dok/render_materials.h"
typedef struct dok_combat_cases_workspace {
    dok_render_world world;
    dok_render_workspace scratch;
    dok_asset_workspace io;
    dok_sprite_asset_bank bank;
    dok_sprite_list list;
    dok_collision_state collision;
    dok_items items;
    dok_combat combat;
    dok_event_scheduler scheduler;
    dok_event_bank events;
    uint32_t ticks,notices[9],hits,wakes,fault;
    int16_t target_health[256];
    unsigned char target_awake[256];
    unsigned char assets[DOK_NDS_ASSETS_BYTES];
} dok_combat_cases_workspace;
typedef struct dok_combat_cases_report {uint32_t checks,failures,line,scenarios,callbacks,trace_hash;} dok_combat_cases_report;
int dok_combat_cases(dok_combat_cases_workspace *,dok_combat_cases_report *,const char *);
#endif
