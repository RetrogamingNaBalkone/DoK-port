#include "runtime.h"
#include "dok/sha256.h"
#include "../assets/resource_paths.h"
#include <nds.h>
#include <stdio.h>
#include <string.h>
#include "ui_native.inc"
#ifndef N21P_AUDIT
#define N21P_AUDIT 1
#endif
#ifndef N23_HUD_FULL_DMA
#define N23_HUD_FULL_DMA 0
#endif
_Alignas(32) volatile uint32_t g_nds_ui_probe[64];
/* DOS HUD10C0:1144 uses C2*4 main-loop calls. Preserve its nominal four
 * seconds on the source PIT clock1193182/119, independent of DS render FPS. */
#define NOTICE_PAGE_TICKS 40107u
int nds_ui_initialize(NdsUiRuntime *u)
{
    unsigned char header[32],digest[32];dok_sha256 hash;FILE *f=fopen(NDS_RESOURCE_ROOT "ui.dat","rb");
    memset(u,0,sizeof(*u));u->lives=3;u->dirty=1;
    if(!f)return 0;
    int ok=fread(header,1,32,f)==32&&!memcmp(header,"N19UI01\0",8)&&header[8]==1u&&header[12]==0u&&header[13]==32u&&header[16]==2u&&
        fread(u->bank,1,DOK_UI_BANK_BYTES,f)==DOK_UI_BANK_BYTES;
    fclose(f);if(!ok)return 0;
    dok_sha256_begin(&hash);dok_sha256_update(&hash,u->bank,DOK_UI_BANK_BYTES);dok_sha256_end(&hash,digest);
    if(memcmp(digest,dok_ui_bank_sha256[0],32))return 0;
    DC_FlushRange(u->bank,DOK_UI_BANK_BYTES);
    return 1;
}
int nds_ui_language(NdsUiRuntime *u,dok_render_world *w,dok_render_workspace *scratch,uint32_t lang)
{
    if(lang<1u||lang>2u)return 0;
    dok_sha256 hash;dok_sha256_begin(&hash);dok_sha256_update(&hash,&w->initial,sizeof(w->initial));dok_sha256_end(&hash,u->language_before);
    dok_sha256_begin(&hash);dok_sha256_update(&hash,&w->presentation,sizeof(w->presentation));dok_sha256_end(&hash,u->simulation_before);
    const dok_level_definition *localized=lang==1u?&w->effective:&w->base;
    if(dok_presentation_bind(&w->effective,localized,w->presentation.session,&scratch->presentation,&u->presentation)!=DOK_BALANCE_OK)return 0;
    if(u->language&&u->language!=lang)++u->language_changes;
    u->language=lang;u->dirty=1;u->page=0;u->message_start=u->ticks;
    dok_sha256_begin(&hash);dok_sha256_update(&hash,&w->initial,sizeof(w->initial));dok_sha256_end(&hash,u->language_after);
    dok_sha256_begin(&hash);dok_sha256_update(&hash,&w->presentation,sizeof(w->presentation));dok_sha256_end(&hash,u->simulation_after);
    if(memcmp(u->language_before,u->language_after,32))return 0;
    if(memcmp(u->simulation_before,u->simulation_after,32))return 0;
    DC_FlushRange(u->language_before,32);DC_FlushRange(u->language_after,32);
    DC_FlushRange(u->simulation_before,32);DC_FlushRange(u->simulation_after,32);
    return 1;
}
void nds_ui_notice(NdsUiRuntime *u,uint32_t kind,uint32_t id,uint32_t slot)
{if(u){u->message_kind=kind;u->message_id=id;u->message_slot=slot;u->message_start=u->ticks;u->page=0;u->dirty=1;}}
static const unsigned char *message(NdsUiRuntime *u,uint32_t *bytes)
{
    *bytes=0;
    if(u->message_kind==2u&&u->message_id<7u){*bytes=dok_ui_native_text_lengths[u->language-1u][u->message_id];return dok_ui_native_text[u->language-1u][u->message_id];}
    if(u->message_kind==1u){dok_presentation_value v;
        if(dok_presentation_get(&u->presentation,(uint8_t)u->message_id,(uint16_t)u->message_slot,&v)==DOK_BALANCE_OK){*bytes=v.text_bytes;return v.text;}}
    return (const unsigned char *)"";
}
/* Explicit display-only fixtures: every original level text and native door
 * string, each page, both languages, with the same live simulation owner. */
void nds_ui_fixture(NdsUiRuntime *u,dok_render_world *w,dok_render_workspace *scratch,uint32_t ordinal)
{
    uint32_t stage=ordinal/4u,total=8u;dok_presentation_value v;
    /* Per-language page counts can differ. Determine both without mutating
     * presentation: immutable localized field spans are read directly. */
    uint32_t per[2]={8u,8u};
    for(uint32_t lang=0;lang<2u;++lang){const dok_level_definition *d=lang?&w->base:&w->effective;
        for(uint32_t o=0;o<d->object_count;++o)for(uint32_t f=0;f<d->objects[o].field_count;++f){const dok_level_field *field=&d->objects[o].fields[f];
            if(field->kind==DOK_LEVEL_TEXT)per[lang]+=dok_ui_paginate(d->source+field->token.offset,field->token.length,30,2,0,NULL,NULL);}
        for(uint32_t i=0;i<7u;++i)per[lang]+=dok_ui_paginate(dok_ui_native_text[lang][i],dok_ui_native_text_lengths[lang][i],30,2,0,NULL,NULL);
    }
    u->stage_count=per[0]+per[1];if(stage>=u->stage_count)stage=u->stage_count-1u;
    uint32_t language=stage<per[0]?1u:2u,local=language==1u?stage:stage-per[0];
    if(u->language!=language&&!nds_ui_language(u,w,scratch,language)){u->fault=1;return;}
    u->stage=stage;u->local_stage=local;u->message_kind=0;u->page=0;
    if(local<8u)return;
    const dok_level_definition *d=language==1u?&w->effective:&w->base;
    for(uint32_t o=0;o<d->object_count;++o)for(uint32_t f=0;f<d->objects[o].field_count;++f){const dok_level_field *field=&d->objects[o].fields[f];
        if(field->kind!=DOK_LEVEL_TEXT)continue;
        if(dok_presentation_get(&u->presentation,d->objects[o].glyph,field->native_offset,&v)!=DOK_BALANCE_OK){u->fault=2;return;}
        uint32_t pages=dok_ui_paginate(v.text,v.text_bytes,30,2,0,NULL,NULL);
        if(local<total+pages){u->message_kind=1;u->message_id=d->objects[o].glyph;u->message_slot=field->native_offset;u->page=local-total;return;}
        total+=pages;
    }
    for(uint32_t i=0;i<7u;++i){uint32_t pages=dok_ui_paginate(dok_ui_native_text[language-1u][i],dok_ui_native_text_lengths[language-1u][i],30,2,0,NULL,NULL);
        if(local<total+pages){u->message_kind=2;u->message_id=i;u->message_slot=0;u->page=local-total;return;}total+=pages;}
}
void nds_ui_update(NdsUiRuntime *u,dok_render_world *w,const dok_ui_values *values,uint32_t ticks,
    unsigned char *buffer,void *vram,uint32_t serial,uint32_t frame)
{
    /* The borrowed mirror and palette share this publication lifetime. Mark
     * them unavailable before the first write, including DMA preparation. */
    uint32_t sequence=g_nds_ui_probe[2]+1u;
    g_nds_ui_probe[2]=sequence;DC_FlushRange((void *)g_nds_ui_probe,32);
    u->ticks=ticks;u->values=*values;uint32_t bytes;
    const unsigned char *data=message(u,&bytes);uint32_t pages=dok_ui_paginate(data,bytes,30,2,0,NULL,NULL);
    if(!u->stage_count){u->page=(ticks-u->message_start)/NOTICE_PAGE_TICKS;
        if(u->page>=pages){if(u->message_kind)u->dirty=1;u->message_kind=0;data=(const unsigned char *)"";bytes=0;u->page=0;pages=1;}}
    u->pages=pages;u->message_bytes=bytes;u->message_address=(uint32_t)(uintptr_t)data;
    /* Draw on changes only. Main BG palette is never written here. */
    uint32_t redrawn=u->dirty||memcmp(&u->last,values,sizeof(*values))||u->last_page!=u->page||u->stage_count;
    if(redrawn){
        dok_ui_hud(buffer,u->bank,u->language,values,data,bytes,u->page);
#if N23_HUD_FULL_DMA
        /* VRAM reads and many short transfers cost more than this 9.5 KiB
         * strip. Keep the same redraw decision and upload its complete rows. */
        DC_FlushRange(buffer,DOK_UI_HUD_BYTES);
        dmaCopy(buffer,vram,DOK_UI_HUD_BYTES);
        u->uploaded_bytes+=DOK_UI_HUD_BYTES;u->uploaded_rows+=38u;
#else
        uint16_t *dst=vram;const uint16_t *src=(const uint16_t *)(const void *)buffer;
        for(uint32_t row=0;row<38u;++row){uint32_t first=0,last=128;
            while(first<last&&src[row*128u+first]==dst[row*128u+first])++first;
            while(last>first&&src[row*128u+last-1u]==dst[row*128u+last-1u])--last;
            if(last>first){uint32_t size=(last-first)*2u;DC_FlushRange(src+row*128u+first,size);dmaCopy(src+row*128u+first,dst+row*128u+first,size);u->uploaded_bytes+=size;++u->uploaded_rows;}}
#endif
        u->last=*values;u->last_page=u->page;u->dirty=0;++u->updates;
        if(N21P_AUDIT){if(memcmp(buffer,vram,DOK_UI_HUD_BYTES))u->fault=3;else ++u->vram_verified;}
    }
    memcpy(u->palette,BG_PALETTE,512);DC_FlushRange(u->palette,512);
#if !N23_HUD_FULL_DMA
    if(redrawn)DC_FlushRange(buffer,DOK_UI_HUD_BYTES);
#endif
    uint32_t p[64]={0x19504b44u,1,sequence,u->language,u->level,w->initial.epoch,serial,
        (uint32_t)(uintptr_t)u->bank,(uint32_t)(uintptr_t)vram,(uint32_t)(int32_t)values->health,(uint32_t)(int32_t)values->charge,
        (uint32_t)(int32_t)values->max_health,(uint32_t)(int32_t)values->max_charge,(uint32_t)(int32_t)values->lives,(uint32_t)(int32_t)values->score,
        values->keys,values->weapon,u->message_kind,u->message_id,u->page,u->pages,u->message_start,u->message_start+NOTICE_PAGE_TICKS*pages,
        u->updates,u->uploaded_bytes,u->uploaded_rows,ticks,u->language_changes,u->fault,u->stage,u->stage_count,u->message_slot,u->message_bytes,u->message_address,frame};
    p[35]=u->local_stage;memcpy(p+36,u->language_before,32);memcpy(p+44,u->language_after,32);p[52]=(uint32_t)(uintptr_t)BG_PALETTE;
    p[53]=(uint32_t)(uintptr_t)buffer;p[54]=u->vram_verified;p[55]=(uint32_t)(uintptr_t)u->palette;
    memcpy(u->published_simulation,u->simulation_before,64);DC_FlushRange(u->published_simulation,64);
    p[56]=(uint32_t)(uintptr_t)u->published_simulation;p[57]=(uint32_t)(uintptr_t)(u->published_simulation+32);
    p[58]=N21P_AUDIT;
    memcpy((void *)g_nds_ui_probe,p,sizeof(p));DC_FlushRange((void *)g_nds_ui_probe,sizeof(p));
    ++g_nds_ui_probe[2];DC_FlushRange((void *)g_nds_ui_probe,32);
}
