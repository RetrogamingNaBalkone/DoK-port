#ifndef NDS_AUDIO_RUNTIME_H
#define NDS_AUDIO_RUNTIME_H
#include "dok/balance.h"
#include "dok/combat.h"
#include "dok/ai.h"
#include "dok/interactions.h"
extern volatile uint32_t g_nds_audio_probe[64];
void nds_audio_runtime_loading(void (*progress)(unsigned));
int nds_audio_runtime_init(const dok_asset_session *,unsigned char *,uint32_t,
    unsigned char *extra,uint32_t extra_bytes);
void nds_audio_runtime_world(const dok_level_presentation *,uint32_t);
int nds_audio_runtime_clear(void);
void nds_audio_runtime_hero_death(dok_object_ref,uint8_t glyph);
void nds_audio_runtime_combat(const dok_combat *,const dok_combat_output *);
void nds_audio_runtime_ai(const dok_ai *,const dok_ai_output *);
void nds_audio_runtime_item(const dok_item_output *);
void nds_audio_runtime_interaction(const dok_interaction_output *);
int nds_audio_runtime_service(void);
int nds_audio_runtime_catalog(void);
int nds_audio_runtime_title(void);
#endif
