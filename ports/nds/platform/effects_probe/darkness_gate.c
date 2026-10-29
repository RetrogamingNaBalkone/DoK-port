/* N13P architecture experiment: actual DS fog at fixed world depths.
 * Independent of source palettes, water and gameplay; uses the same backend
 * and accepted projection/depth mode as the accumulated renderer route. */
#include <nds.h>
#include <string.h>
#include "render_backend.h"
static dok_nds_render_quad quads[8];
static dok_nds_render_cell cells[8];
static unsigned char pixels[64];
static uint16_t palette[256];
_Alignas(32) volatile uint32_t g_nds_darkness_probe[16];
int main(void)
{
    static const int32_t depths[8]={600,1600,1920,2400,3000,3600,4000,4800};
    dok_nds_render_stats stats;dok_nds_stream_stats stream;
    dok_nds_render_frame frame={{0,0,0,0,32768},cells,8,1};
    if(dok_nds_render_init()!=DOK_NDS_RENDER_OK)goto fail;
    memset(pixels,1,sizeof(pixels));palette[1]=32767;
    dok_nds_stream_texture texture={1,{1,pixels,64,palette,8,8,8,8,0}};
    if(dok_nds_stream_reset(palette,256)!=DOK_NDS_RENDER_OK)goto fail;
    for(uint32_t i=0;i<8u;++i) {
        int32_t d=depths[i],z=d-200,origin=z/512*512;
        int32_t x=((int32_t)i*32+16-127)*d/160,half=d/16,y=d/6;
        quads[i].texture_slot=UINT16_MAX;quads[i].color_rgb555=32767;quads[i].source_surface_id=i+1u;
        quads[i].vertex[0]=(dok_nds_render_vertex){x-half,-y,z-origin,0,0};
        quads[i].vertex[1]=(dok_nds_render_vertex){x+half,-y,z-origin,0,0};
        quads[i].vertex[2]=(dok_nds_render_vertex){x+half,y,z-origin,0,0};
        quads[i].vertex[3]=(dok_nds_render_vertex){x-half,y,z-origin,0,0};
        cells[i]=(dok_nds_render_cell){0,0,origin,&quads[i],1};
    }
    dok_nds_render_darkness(1);
    if(dok_nds_stream_plan(&texture,1,&stream)!=DOK_NDS_RENDER_OK)goto fail;
    swiWaitForVBlank();
    if(dok_nds_stream_commit(&stream)!=DOK_NDS_RENDER_OK)goto fail;
    if(dok_nds_render_submit(&frame,&stats)!=DOK_NDS_RENDER_OK)goto fail;
    swiWaitForVBlank();
    dok_nds_render_present();
    for(uint32_t i=0;i<4u;++i)swiWaitForVBlank();
    g_nds_darkness_probe[0]=0x50464b44;g_nds_darkness_probe[1]=1;
    for(uint32_t i=0;i<8u;++i)g_nds_darkness_probe[2+i]=(uint32_t)depths[i];
    DC_FlushRange((const void *)g_nds_darkness_probe,sizeof(g_nds_darkness_probe));
    while(1)swiWaitForVBlank();
fail:
    g_nds_darkness_probe[0]=0x50464b44;g_nds_darkness_probe[1]=2;
    DC_FlushRange((const void *)g_nds_darkness_probe,sizeof(g_nds_darkness_probe));
    while(1)swiWaitForVBlank();
}
