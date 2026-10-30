/* Controlled public-SDK allocation experiment. This is not a gameplay route. */
#include <nds.h>
#include <stdint.h>
#include <string.h>
#include "render_backend.h"
typedef struct RootTexture {uint64_t key;uint32_t asset,gpu,bytes;uint16_t width,height;} RootTexture;
#include "fragment_layout.inc"
typedef struct SdkRecord {uint32_t phase,index,key_lo,key_hi,address,bytes,sdk_id,first_byte;} SdkRecord;
_Alignas(32) volatile uint32_t g_nds_fragment_probe[64];
_Alignas(32) SdkRecord g_nds_fragment_records[96];
_Alignas(32) unsigned char g_nds_fragment_vram[393216];
_Alignas(32) uint16_t g_nds_fragment_palette[4096];
static unsigned char pool[460800],replacement[16384],before[393216],map_pixels[49152];
static uint16_t palette[4096];
static dok_nds_stream_texture prefix[32],previous[16],final[19];
static dok_nds_stream_stats planned;
static uint32_t p[64],records,checks,pool_used;
#define NEED(e) do{++checks;if(!(e))fail(__LINE__);}while(0)
static void publish(uint32_t status)
{
    uint32_t sequence=(g_nds_fragment_probe[2]+1u)|1u;
    g_nds_fragment_probe[2]=sequence;DC_FlushRange((void *)g_nds_fragment_probe,32);
    p[0]=0x22464b44u;p[1]=1;p[2]=sequence;p[3]=status;p[5]=checks;
    p[6]=N22_STREAM_PARTIAL_DEFRAG;p[28]=(uint32_t)(uintptr_t)g_nds_fragment_records;p[29]=records;p[30]=sizeof(SdkRecord);
    p[38]=(uint32_t)(uintptr_t)g_nds_fragment_vram;p[39]=(uint32_t)(uintptr_t)g_nds_fragment_palette;p[40]=isDSiMode();
    DC_FlushRange(g_nds_fragment_records,sizeof(g_nds_fragment_records));
    memcpy((void *)g_nds_fragment_probe,p,sizeof(p));DC_FlushRange((void *)g_nds_fragment_probe,sizeof(p));
    ++g_nds_fragment_probe[2];DC_FlushRange((void *)g_nds_fragment_probe,32);
}
static void fail(uint32_t line){p[4]=line;publish(3);for(;;)swiWaitForVBlank();}
static int id_at(uint32_t address)
{for(int id=1;id<256;++id)if((uint32_t)(uintptr_t)glGetTexturePointer(id)==address)return id;return 0;}
static void copy_vram(unsigned char *dst,uint16_t *pal)
{
    uint32_t power=REG_POWERCNT;powerOff(POWER_3D_CORE);
    uint32_t banks=vramSetPrimaryBanks(VRAM_A_LCD,VRAM_B_LCD,VRAM_C_SUB_BG,VRAM_D_LCD);
    memcpy(dst,(void *)0x06800000u,262144);memcpy(dst+262144,(void *)0x06860000u,131072);
    vramRestorePrimaryBanks(banks);
    if(pal){uint8_t old=VRAM_F_CR;vramSetBankF(VRAM_F_LCD);memcpy(pal,(void *)0x06890000u,8192);VRAM_F_CR=old;}
    if(power&POWER_3D_CORE)powerOn(POWER_3D_CORE);
}
static const unsigned char *snapshot_pointer(uint32_t address)
{return address<0x06840000u?g_nds_fragment_vram+address-0x06800000u:g_nds_fragment_vram+262144u+address-0x06860000u;}
static uint32_t banks(void)
{return VRAM_A_CR|((uint32_t)VRAM_B_CR<<8)|((uint32_t)VRAM_C_CR<<16)|((uint32_t)VRAM_D_CR<<24);}
static dok_nds_stream_texture make_texture(const RootTexture *r,uint32_t ordinal)
{
    NEED(pool_used+r->bytes<=sizeof(pool));unsigned char *pixels=pool+pool_used;pool_used+=r->bytes;
    for(uint32_t i=0;i<r->bytes;++i)pixels[i]=(unsigned char)((ordinal*11u+17u+i*37u+(i>>8u)*13u)%254u+1u);
    return (dok_nds_stream_texture){r->key,{r->asset,pixels,r->bytes,palette,r->width,r->height,r->width,r->height,0}};
}
static void save_records(uint32_t phase,const dok_nds_stream_texture *textures,uint32_t count,const RootTexture *exact)
{
    copy_vram(g_nds_fragment_vram,g_nds_fragment_palette);
    NEED(memcmp(g_nds_fragment_palette,palette,sizeof(palette))==0);
    for(uint32_t n=0;n<count;++n){int found=0;uint32_t address=0;
        for(int id=1;id<256;++id){void *pointer=glGetTexturePointer(id);if(!pointer)continue;
            glBindTexture(0,id);int width=0,height=0;glGetInt(GL_GET_TEXTURE_WIDTH,&width);glGetInt(GL_GET_TEXTURE_HEIGHT,&height);
            if((uint32_t)width*height!=textures[n].texture.bytes)continue;
            uint32_t candidate=(uint32_t)(uintptr_t)pointer;
            if((candidate>=0x06800000u&&candidate+textures[n].texture.bytes<=0x06840000u)||
               (candidate>=0x06860000u&&candidate+textures[n].texture.bytes<=0x06880000u)){
                if(memcmp(snapshot_pointer(candidate),textures[n].texture.pixels,textures[n].texture.bytes)==0){found=id;address=candidate;break;}
            }
        }
        NEED(found);if(exact)NEED(address==exact[n].gpu);
        NEED(records<96u);g_nds_fragment_records[records++]=(SdkRecord){phase,n,(uint32_t)textures[n].key,
            (uint32_t)(textures[n].key>>32),address,textures[n].texture.bytes,(uint32_t)found,textures[n].texture.pixels[0]};
    }
    DC_FlushRange(g_nds_fragment_vram,sizeof(g_nds_fragment_vram));
    DC_FlushRange(g_nds_fragment_palette,sizeof(g_nds_fragment_palette));
}
static void submit_empty(void)
{
    dok_nds_render_cell cell={0,0,0,NULL,0};dok_nds_render_frame frame={{0,0,0,0,32768},&cell,1,1};dok_nds_render_stats stats;
    NEED(dok_nds_render_submit(&frame,&stats)==DOK_NDS_RENDER_OK);swiWaitForVBlank();dok_nds_render_present();
}
static void queue_final(void)
{
    static dok_nds_render_quad quads[19];
    for(uint32_t i=0;i<19u;++i){int32_t x=((int32_t)(i%5u)*52+24-128)*2048/160;
        int32_t y=((int32_t)(i/5u)*37+18-77)*2048/192;
        dok_nds_render_quad *q=&quads[i];q->texture_slot=(uint16_t)i;q->color_rgb555=32767;
        q->vertex[0]=(dok_nds_render_vertex){x-180,y-90,2048,0,0};
        q->vertex[1]=(dok_nds_render_vertex){x+180,y-90,2048,0,0};
        q->vertex[2]=(dok_nds_render_vertex){x+180,y+90,2048,0,0};
        q->vertex[3]=(dok_nds_render_vertex){x-180,y+90,2048,0,0};
    }
    dok_nds_render_cell cell={0,0,0,quads,19};dok_nds_render_frame frame={{0,0,0,0,32768},&cell,1,2};dok_nds_render_stats stats;
    NEED(dok_nds_render_submit(&frame,&stats)==DOK_NDS_RENDER_OK);
    p[41]=stats.submitted_quads;p[42]=stats.hardware_polygons;p[43]=stats.disp3d_flags;p[44]=stats.gxstat_flags;
}
static void draw_final(void)
{queue_final();swiWaitForVBlank();dok_nds_render_present();swiWaitForVBlank();swiWaitForVBlank();}
int main(void)
{
    cpuStartTiming(0);publish(1);NEED(!isDSiMode());
    NEED(dok_nds_render_init()==DOK_NDS_RENDER_OK);
    for(uint32_t i=0;i<4096u;++i){uint32_t c=i&255u;palette[i]=(uint16_t)(((c*3u)&31u)|(((c*5u)&31u)<<5)|(((c*7u)&31u)<<10));}
    for(uint32_t i=0;i<sizeof(map_pixels);++i)map_pixels[i]=(unsigned char)(i*7u+3u);
    DC_FlushRange(map_pixels,sizeof(map_pixels));
    NEED(dok_nds_stream_reset(palette,4096)==DOK_NDS_RENDER_OK);
    /* Singleton-prefix plans make address order independent of missing-sort. */
    for(uint32_t n=0;n<21u;++n){prefix[n]=make_texture(&root_prefix[n],n);
        NEED(dok_nds_stream_plan(prefix,n+1u,&planned)==DOK_NDS_RENDER_OK);
        NEED(id_at(root_prefix[n].gpu));NEED(dok_nds_stream_commit(&planned)==DOK_NDS_RENDER_OK);
    }
    save_records(0,prefix,21,root_prefix);p[7]=1;publish(1);
    for(uint32_t n=0;n<16u;++n){uint32_t i=0;while(prefix[i].key!=root_previous[n].key)++i;previous[n]=prefix[i];}
    NEED(dok_nds_stream_plan(previous,16,&planned)==DOK_NDS_RENDER_OK);
    NEED(planned.hits==16&&planned.evictions==5&&planned.upload_bytes==0);
    NEED(dok_nds_stream_commit(&planned)==DOK_NDS_RENDER_OK);save_records(1,previous,16,root_previous);
    for(uint32_t n=0;n<19u;++n){uint32_t i=0;while(i<16u&&previous[i].key!=root_final[n].key)++i;
        final[n]=i<16u?previous[i]:make_texture(&root_final[n],21u+n);
        if(final[n].key==UINT64_C(5398598656)){
            memcpy(replacement,final[n].texture.pixels,16384);memset((void *)final[n].texture.pixels,0xe7,16384);
            DC_FlushRange(final[n].texture.pixels,16384);final[n].texture.pixels=replacement;p[32]=16384;
        }
    }
    submit_empty();copy_vram(before,NULL);p[46]=banks();p[48]=VRAM_F_CR;
    NEED(dok_nds_stream_plan(final,19,&planned)==DOK_NDS_RENDER_OK);p[8]=0;
    copy_vram(g_nds_fragment_vram,g_nds_fragment_palette);
    p[47]=banks();p[49]=VRAM_F_CR;NEED(p[46]==p[47]&&p[48]==p[49]);
    p[24]=memcmp(before,g_nds_fragment_vram,sizeof(before))==0;NEED(p[24]);
    p[26]=memcmp(palette,g_nds_fragment_palette,sizeof(palette))==0;NEED(p[26]);
    p[10]=planned.upload_bytes;p[11]=planned.uploads;p[12]=planned.hits;p[13]=planned.misses;
    p[14]=planned.evictions;p[15]=planned.rebuilds;p[16]=planned.resident_bytes;p[17]=planned.resident_count;
#if N22_STREAM_PARTIAL_DEFRAG
    NEED(planned.upload_bytes==67840u&&planned.uploads==5u&&planned.hits==14u);
#else
    NEED(planned.upload_bytes==367360u&&planned.uploads==19u&&planned.hits==0u);
#endif
#if N22_STREAM_PARTIAL_DEFRAG
    /* Match production: queue geometry with the new SDK addresses before
     * the presentation boundary and upload its texels after the swap. */
    queue_final();
#endif
    NEED(dok_nds_stream_prepare_uploads()==DOK_NDS_RENDER_OK);
    /* Preserve the same ordering and bounds as the accepted production path:
     * 49KiB map first in VBlank, then GPU uploads within the same119-line epoch. */
    swiWaitForVBlank();uint32_t epoch=cpuGetTiming();p[20]=REG_VCOUNT;
    dmaCopy(map_pixels,(void *)0x06200000u,sizeof(map_pixels));p[21]=REG_VCOUNT;
    p[9]=dok_nds_stream_commit_prepared(&planned);p[22]=cpuGetTiming()-epoch;
    p[50]=banks();p[51]=VRAM_F_CR;p[27]=p[46]==p[50]&&p[48]==p[51];NEED(p[27]);
    p[18]=planned.commit_start_line;p[19]=planned.commit_end_line;p[23]=p[22]<119u*2130u;
    NEED(p[20]>=192u&&p[21]<=262u&&p[20]<=p[21]);
    p[37]=memcmp(map_pixels,(void *)0x06200000u,sizeof(map_pixels))==0;NEED(p[37]);
#if N22_STREAM_PARTIAL_DEFRAG
    NEED(p[9]==DOK_NDS_RENDER_OK&&p[23]);
#else
    NEED(p[9]==DOK_NDS_RENDER_HARDWARE&&!p[23]);
#endif
    save_records(2,final,19,NULL);p[25]=0;p[33]=1;
    /* Independently exercise the active-raster guard with a real pending
     * palette update. A refusal must leave every texture byte unchanged. */
    NEED(dok_nds_stream_palette_dirty()==DOK_NDS_RENDER_OK);
    NEED(dok_nds_stream_plan(final,19,&planned)==DOK_NDS_RENDER_OK);
    copy_vram(before,NULL);while(REG_VCOUNT!=100u){}
    p[34]=dok_nds_stream_commit_prepared(&planned);NEED(p[34]==DOK_NDS_RENDER_HARDWARE);
    copy_vram(g_nds_fragment_vram,NULL);p[35]=memcmp(before,g_nds_fragment_vram,sizeof(before))==0;NEED(p[35]);
    swiWaitForVBlank();NEED(dok_nds_stream_commit(&planned)==DOK_NDS_RENDER_OK);
    if(N22_STREAM_PARTIAL_DEFRAG)draw_final();
    DC_FlushRange(g_nds_fragment_vram,sizeof(g_nds_fragment_vram));p[7]=3;publish(2);
    for(;;){swiWaitForVBlank();++g_nds_fragment_probe[45];DC_FlushRange((void *)g_nds_fragment_probe,sizeof(p));}
}
