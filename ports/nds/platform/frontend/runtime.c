#include "runtime.h"
#include "dok/ui.h"
#include "../assets/resource_paths.h"
#include "dok/sha256.h"
#include "frontend_hash.inc"
#include <nds.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#ifndef N21P_AUDIT
#define N21P_AUDIT 1
#endif
_Alignas(32) volatile uint32_t g_nds_frontend_probe[64];
static volatile uint32_t early_keys,early_active=1,input_edges,input_previous;
void nds_frontend_early_sample(void)
{uint32_t keys=(~REG_KEYINPUT)&(KEY_A|KEY_B|KEY_START|KEY_UP|KEY_DOWN);
 input_edges|=keys&~input_previous;input_previous=keys;
 if(early_active)early_keys|=keys&(KEY_A|KEY_START);}
uint32_t nds_frontend_take_edges(void)
{uint32_t irq=enterCriticalSection(),keys=input_edges;input_edges=0;leaveCriticalSection(irq);return keys;}
void nds_frontend_boot_probe(void)
{g_nds_frontend_probe[0]=0x21504b44u;g_nds_frontend_probe[1]=1;DC_FlushRange((void *)g_nds_frontend_probe,256);}
static uint32_t le32(const unsigned char *p)
{return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static NdsFrontendEntry *entry(NdsFrontend *f,uint32_t kind,uint32_t id)
{for(unsigned i=0;i<38u;++i)if(f->directory[i].kind==kind&&f->directory[i].id==id)return &f->directory[i];return NULL;}
static int read_entry(NdsFrontend *f,uint32_t kind,uint32_t id,void *out,uint32_t capacity,uint32_t *bytes)
{
    NdsFrontendEntry *e=entry(f,kind,id);
    if(!e||e->bytes>capacity||fseek(f->file,(long)e->offset,SEEK_SET)||fread(out,1,e->bytes,f->file)!=e->bytes)return 0;
    *bytes=e->bytes;return 1;
}
int nds_frontend_initialize(NdsFrontend *f,const dok_asset_session *assets,void *scratch,const unsigned char *font)
{
    unsigned char h[32],raw[608],settings[17];uint16_t pages[2][8];uint8_t language=1;
    memset(f,0,sizeof(*f));f->assets=assets;f->font=font;f->top=scratch;f->bottom=f->top+49152;f->text=f->top+98304;
    f->file=fopen(NDS_RESOURCE_ROOT "frontend.dat","rb");
    if(!f->file||fread(h,1,32,f->file)!=32||memcmp(h,"N21UI01\0",8)||le32(h+8)!=1||le32(h+12)!=38||le32(h+16)!=32||le32(h+24)||le32(h+28)||fread(raw,1,sizeof(raw),f->file)!=sizeof(raw))return 0;
    if(le32(h+20)!=DOK_FRONTEND_FILE_BYTES||fseek(f->file,0,SEEK_END)||ftell(f->file)!=(long)le32(h+20))return 0;
    {dok_sha256 hash;unsigned char digest[32];uint32_t left=DOK_FRONTEND_FILE_BYTES;
     dok_sha256_begin(&hash);if(fseek(f->file,0,SEEK_SET))return 0;
     while(left){unsigned n=left>1024u?1024u:left;if(fread(f->text,1,n,f->file)!=n)return 0;dok_sha256_update(&hash,f->text,n);left-=n;}
     dok_sha256_end(&hash,digest);if(memcmp(digest,dok_frontend_file_sha256,32))return 0;}
    for(unsigned i=0;i<38u;++i){NdsFrontendEntry *e=&f->directory[i];e->kind=le32(raw+i*16);e->id=le32(raw+i*16+4);e->offset=le32(raw+i*16+8);e->bytes=le32(raw+i*16+12);
        if(e->offset<640u||e->offset>le32(h+20)||e->bytes>le32(h+20)-e->offset)return 0;}
    for(unsigned lang=1;lang<=2u;++lang)for(unsigned id=0;id<8u;++id){uint32_t bytes;
        if(!read_entry(f,1,lang*16u+id,f->text,32768,&bytes))return 0;
        pages[lang-1][id]=(uint16_t)dok_ui_paginate(f->text,bytes,30,9,0,NULL,NULL);}
    FILE *settings_file=fopen("fat:/dok/settings.dat","rb");
    if(settings_file){size_t n=fread(settings,1,sizeof(settings),settings_file);int io=ferror(settings_file);fclose(settings_file);
        f->settings_status=!io&&dok_settings_decode(settings,n,&language)?1u:2u;}
    dok_frontend_init(&f->state,language,(const uint16_t (*)[8])pages);
    if(dok_frontend_visual_init(&f->visual,assets)!=DOK_STATUS_OK)return 0;
    f->early_keys=early_keys;early_active=0;
    if(f->early_keys)(void)dok_frontend_step(&f->state,f->early_keys,0,0);
    f->background=UINT32_MAX;f->text_index=UINT32_MAX;
    return 1;
}
int nds_frontend_settings_write(NdsFrontend *f)
{
    unsigned char bytes[16];dok_settings_encode(bytes,f->state.language);
    /* No data directory is shipped with a standalone ROM. Create it only
     * when settings are changed; a read-only/missing drive is non-fatal. */
    if(mkdir("fat:/dok",0777)!=0&&errno!=EEXIST){++f->settings_failures;return 0;}
    FILE *file=fopen("fat:/dok/settings.dat","wb");
    if(!file){++f->settings_failures;return 0;}
    int ok=fwrite(bytes,1,16,file)==16;if(fflush(file))ok=0;if(fclose(file))ok=0;
    if(ok)++f->settings_writes;else ++f->settings_failures;return ok;
}
static void text(NdsFrontend *f,unsigned char *out,const unsigned char *s,uint32_t n,unsigned x,unsigned y,unsigned color)
{for(unsigned c=0;c<n&&x+c*8u+8u<=256u;++c)for(unsigned r=0;r<8u;++r)for(unsigned b=0;b<8u;++b)
    if(f->font[s[c]*8u+r]&(128u>>b))out[(y+r)*256u+x+c*8u+b]=(unsigned char)color;}
static void label(NdsFrontend *f,const unsigned char *s,unsigned x,unsigned y,unsigned color)
{text(f,f->bottom,s,(uint32_t)strlen((const char *)s),x,y,color);}
#include "labels.inc"
static void line(NdsFrontend *f,const unsigned char *s,unsigned row)
{
    unsigned y=28u+24u*row,color=2;
    if(row==f->state.selection){for(unsigned r=0;r<20u;++r)memset(f->bottom+(y+r)*256u+8,2,240);color=0;label(f,(const unsigned char *)">",16,y+4,color);}
    label(f,s,32,y+4,color);
}
typedef struct PageDraw {NdsFrontend *f;const unsigned char *data;} PageDraw;
static void page_glyph(void *p,uint32_t offset,uint32_t col,uint32_t row)
{PageDraw *d=p;NdsFrontend *f=d->f;unsigned x=8u+8u*col,y=24u+16u*row;
 for(unsigned r=0;r<8u;++r)for(unsigned b=0;b<8u;++b)
    if(f->font[d->data[offset]*8u+r]&(128u>>b))f->bottom[(y+4u+r)*256u+x+b]=2;}
static int background(NdsFrontend *f,unsigned id)
{
    unsigned char links[8];uint32_t bytes;dok_asset_view view;
    if(!read_entry(f,3,f->state.language*16u+id,links,8,&bytes)||bytes!=8u)return 0;
    uint32_t image=le32(links),palette=le32(links+4);
    if(dok_assets_load(f->assets,image,f->top,49152,&view)!=DOK_ASSET_OK)return 0;
    if(id==7u){if(view.entry.width!=46u||view.entry.height!=46u)return 0;memcpy(f->text,f->top,46u*46u);
        for(unsigned y=0;y<192u;++y)for(unsigned x=0;x<256u;++x)f->top[y*256u+x]=f->text[(y%46u)*46u+x%46u];}
    else if(view.entry.bytes!=49152u)return 0;
    if(dok_assets_load(f->assets,palette,(unsigned char *)f->palettes[0],512,&view)!=DOK_ASSET_OK||view.entry.bytes!=512u)return 0;
    /* Lower text uses the original FON palette, independent of story lighting. */
    if(!read_entry(f,3,f->state.language*16u+7u,links,8,&bytes)||bytes!=8u||
       dok_assets_load(f->assets,le32(links+4),(unsigned char *)f->palettes[1],512,&view)!=DOK_ASSET_OK)return 0;
    f->background=id;return 1;
}
int nds_frontend_draw(NdsFrontend *f,void *top,void *bottom)
{
    if(f->drawn_revision==f->state.revision)return 1;
    if(!(g_nds_frontend_probe[2]&1u))++g_nds_frontend_probe[2];
    DC_FlushRange((void *)g_nds_frontend_probe,32);
    unsigned state=f->state.state,lang=f->state.language-1u,bg=7u;
    int index=dok_frontend_text_index(&f->state);uint32_t bytes;
    if(state==DOK_FRONTEND_TITLE||state==DOK_FRONTEND_STORY)bg=0;
    else if(state==DOK_FRONTEND_INTRO||state==DOK_FRONTEND_OUTRO)bg=(unsigned)index;
    if(!background(f,bg))return 0;
    memset(f->bottom,0,49152);f->text_index=index<0?UINT32_MAX:(uint32_t)index;f->text_bytes=0;
    if(state==DOK_FRONTEND_TITLE){label(f,labels[lang][0],8,8,2);label(f,(const unsigned char *)"A / Start",88,88,2);}
    else if(state==DOK_FRONTEND_MAIN){label(f,labels[lang][1],8,8,2);for(unsigned i=0;i<5u;++i)line(f,labels[lang][2+i],i);}
    else if(state==DOK_FRONTEND_PAUSE){label(f,labels[lang][7],8,8,2);for(unsigned i=0;i<4u;++i)line(f,labels[lang][8+i],i);}
    else if(state==DOK_FRONTEND_LEVELS){label(f,labels[lang][12],8,8,2);for(unsigned i=0;i<3u;++i){if(!read_entry(f,2,f->state.language*16u+i+1u,f->text,32767,&bytes))return 0;f->text[bytes]=0;line(f,f->text,i);}}
    else if(state==DOK_FRONTEND_LANGUAGE){label(f,labels[lang][3],8,8,2);line(f,labels[0][13],0);line(f,labels[1][13],1);}
    else if(state==DOK_FRONTEND_CONTROLS){label(f,labels[lang][4],8,8,2);for(unsigned i=0;i<11u;++i)label(f,controls[lang][i],8,24u+i*12u,2);}
    else if(state==DOK_FRONTEND_EXIT){label(f,labels[lang][6],8,8,2);label(f,labels[lang][16],8,80,2);}
    else if(index>=0){
        label(f,labels[lang][index==0?5:state==DOK_FRONTEND_INTRO?17:state==DOK_FRONTEND_OUTRO?18:19],8,8,2);
        if(!read_entry(f,1,f->state.language*16u+(unsigned)index,f->text,32768,&bytes))return 0;
        f->text_bytes=bytes;PageDraw draw={f,f->text};
        uint32_t pages=dok_ui_paginate(f->text,bytes,30,9,f->state.page,page_glyph,&draw);
        if(pages!=f->state.page_count)return 0;
        unsigned char number[7];unsigned n=dok_ui_number((int16_t)(f->state.page+1u),number);text(f,f->bottom,number,n,208,8,2);
        text(f,f->bottom,(const unsigned char *)"/",1,224,8,2);n=dok_ui_number((int16_t)pages,number);text(f,f->bottom,number,n,232,8,2);
    }
    if(state!=DOK_FRONTEND_TITLE&&state!=DOK_FRONTEND_EXIT)label(f,labels[lang][index>=0?15:14],8,176,2);
#if FRONTEND_FIXTURES
    if(state==DOK_FRONTEND_CONTROLS)label(f,(const unsigned char *)"Fixture: Sel+Start/X/Y",8,160,2);
#endif
    /* Menus share one screen with their original tiled background. Only
     * title/story/intermission pages use the second display for text. */
    if(index<0&&state!=DOK_FRONTEND_TITLE){
        /* Index zero is also the selected row's foreground. Composite the
         * complete menu canvas over FON rather than treating zero as alpha. */
        for(unsigned y=0;y<192u;++y)for(unsigned x=0;x<256u;++x){
            unsigned at=y*256u+x;
            if(f->bottom[at]|| (x>=8u&&x<248u&&y>=28u+24u*f->state.selection&&y<48u+24u*f->state.selection&&
               (state==DOK_FRONTEND_MAIN||state==DOK_FRONTEND_PAUSE||state==DOK_FRONTEND_LEVELS||state==DOK_FRONTEND_LANGUAGE)))
                f->top[at]=f->bottom[at];
        }
        memset(f->bottom,0,49152);
        memcpy(f->palettes[0],f->palettes[1],512);
    }
    DC_FlushRange(f->top,49152);DC_FlushRange(f->bottom,49152);
    if(f->text_bytes)DC_FlushRange(f->text,f->text_bytes);
    DC_FlushRange(f->palettes,sizeof(f->palettes));
    dmaCopy(f->top,top,49152);dmaCopy(f->bottom,bottom,49152);
    for(unsigned n=0;n<256u;++n){BG_PALETTE[n]=f->palettes[0][n];BG_PALETTE_SUB[n]=f->palettes[1][n];}
    if(N21P_AUDIT){
        if(memcmp(f->top,top,49152)||memcmp(f->bottom,bottom,49152))return 0;
        ++f->top_verified;++f->bottom_verified;
    }
    ++f->draws;f->drawn_revision=f->state.revision;return 1;
}
void nds_frontend_publish(NdsFrontend *f,const uint32_t game[16])
{
    uint32_t seq=g_nds_frontend_probe[2]|1u;g_nds_frontend_probe[2]=seq;DC_FlushRange((void *)g_nds_frontend_probe,32);
    uint32_t p[64]={0x21504b44u,1,seq,f->state.state,f->state.language,f->state.selection,f->state.level,f->state.page,f->state.page_count,
        f->state.blocked,f->state.title_pending,f->state.revision,f->state.transitions,f->ready,f->early_keys,f->actions,
        f->text_index,f->text_bytes,(uint32_t)(uintptr_t)f->text,f->background,(uint32_t)(uintptr_t)f->top,(uint32_t)(uintptr_t)f->bottom,
        (uint32_t)(uintptr_t)f->palettes[0],(uint32_t)(uintptr_t)f->palettes[1],f->draws,f->top_verified,f->bottom_verified,
        f->settings_status,f->settings_writes,f->settings_failures,f->fault,FRONTEND_FIXTURES,f->frames};
    if(game)memcpy(p+40,game,16u*4u);
    p[33]=(uint32_t)(uintptr_t)f->visual_descriptor;p[34]=f->visual_descriptor_bytes;
    p[35]=f->visual_frame;p[36]=f->visual_slot;p[37]=f->visual_references;
    p[60]=f->visual_asset;p[61]=f->visual_pixels;p[62]=f->visual_bytes;p[63]=f->visual_fixture;
    /* Static payload was flushed at its draw/mutation boundary. Raw inspector
     * bytes need another clean only after a cache insertion/compaction or a
     * changed asset/address/span, never merely because a frame elapsed. */
    if(f->state.state==DOK_FRONTEND_PLAY&&f->visual_pixels&&f->visual_bytes&&
       (f->visual_dirty||f->visual_flushed_asset!=f->visual_asset||
        f->visual_flushed_pixels!=f->visual_pixels||f->visual_flushed_bytes!=f->visual_bytes)){
        DC_FlushRange((void *)(uintptr_t)f->visual_pixels,f->visual_bytes);
        f->visual_flushed_asset=f->visual_asset;f->visual_flushed_pixels=f->visual_pixels;
        f->visual_flushed_bytes=f->visual_bytes;
    }
    f->visual_dirty=0;
    DC_FlushRange(f->visual_descriptor,sizeof(f->visual_descriptor));
    p[38]=N21P_AUDIT;
    memcpy((void *)g_nds_frontend_probe,p,sizeof(p));DC_FlushRange((void *)g_nds_frontend_probe,256);
    ++g_nds_frontend_probe[2];DC_FlushRange((void *)g_nds_frontend_probe,32);
}
