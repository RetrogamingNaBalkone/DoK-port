#ifndef DOK_ITEMS_CASES_H
#define DOK_ITEMS_CASES_H
#include "dok/items.h"
#include "dok/render_materials.h"
#include "dok/interactions.h"
typedef struct dok_items_cases_workspace {
    dok_render_world world;
    dok_render_workspace scratch;
    dok_asset_workspace io;
    dok_collision_state collision;
    dok_items items;
    dok_interactions_state interactions;
    dok_event_scheduler scheduler;
    dok_event_bank events;
    unsigned char assets[DOK_NDS_ASSETS_BYTES];
} dok_items_cases_workspace;
typedef struct dok_items_cases_report { uint32_t checks,failures,line,placements,notifications; } dok_items_cases_report;
int dok_items_cases(dok_items_cases_workspace *,dok_items_cases_report *,const char *package);
#endif
