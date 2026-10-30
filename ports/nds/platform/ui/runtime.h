#ifndef NDS_UI_RUNTIME_H
#define NDS_UI_RUNTIME_H
#include "dok/ui.h"
#include "dok/render_materials.h"
typedef struct NdsUiRuntime {
    unsigned char bank[DOK_UI_BANK_BYTES];
    dok_level_presentation presentation;
    dok_ui_values values,last;
    uint32_t language,level,ticks,message_start,message_kind,message_id,message_slot;
    uint32_t page,pages,updates,uploaded_bytes,uploaded_rows,language_changes,fault;
    uint32_t dirty,stage,stage_count,message_bytes,message_address,last_page,local_stage;
    unsigned char language_before[32],language_after[32];
    unsigned char simulation_before[32],simulation_after[32];
    unsigned char published_simulation[64];
    uint16_t palette[256];
    uint32_t vram_verified;
    int16_t lives;
    uint16_t overlay_blank;
} NdsUiRuntime;
extern volatile uint32_t g_nds_ui_probe[64];
int nds_ui_initialize(NdsUiRuntime *);
int nds_ui_language(NdsUiRuntime *,dok_render_world *,dok_render_workspace *,uint32_t);
void nds_ui_notice(NdsUiRuntime *,uint32_t kind,uint32_t id,uint32_t slot);
void nds_ui_fixture(NdsUiRuntime *,dok_render_world *,dok_render_workspace *,uint32_t ordinal);
void nds_ui_update(NdsUiRuntime *,dok_render_world *,const dok_ui_values *,uint32_t,
    unsigned char *scratch,void *vram,uint32_t serial,uint32_t frame);
#endif
