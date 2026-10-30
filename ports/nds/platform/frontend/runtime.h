#ifndef NDS_FRONTEND_RUNTIME_H
#define NDS_FRONTEND_RUNTIME_H
#include "dok/frontend.h"
#include "dok/frontend_visual.h"
#include "dok/assets.h"
#include <stdio.h>
#ifndef FRONTEND_FIXTURES
#define FRONTEND_FIXTURES 0
#endif
typedef struct NdsFrontendEntry { uint32_t kind,id,offset,bytes; } NdsFrontendEntry;
typedef struct NdsFrontend {
    dok_frontend state;
    dok_frontend_visual visual;
    NdsFrontendEntry directory[38];
    uint16_t palettes[2][256];
    FILE *file;
    unsigned char *top,*bottom,*text;
    const unsigned char *font;
    const dok_asset_session *assets;
    uint32_t text_bytes,text_index,background,drawn_revision,draws,fault;
    uint32_t settings_status,settings_writes,settings_failures,actions;
    uint32_t top_verified,bottom_verified,early_keys,ready,frames;
    uint32_t visual_fixture,visual_asset,visual_pixels,visual_bytes,visual_chord;
    uint32_t visual_descriptor[10],visual_descriptor_bytes,visual_frame,visual_slot,visual_references;
    uint32_t visual_dirty,visual_flushed_asset,visual_flushed_pixels,visual_flushed_bytes;
} NdsFrontend;
extern volatile uint32_t g_nds_frontend_probe[64];
void nds_frontend_early_sample(void);
uint32_t nds_frontend_take_edges(void);
void nds_frontend_boot_probe(void);
int nds_frontend_initialize(NdsFrontend *,const dok_asset_session *,void *scratch,const unsigned char *font);
int nds_frontend_draw(NdsFrontend *,void *top,void *bottom);
int nds_frontend_settings_write(NdsFrontend *);
void nds_frontend_publish(NdsFrontend *,const uint32_t game[16]);
#endif
