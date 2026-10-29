/* N07P actual ARM9 tests for both sides of the conservative capacity proof.
 * No game fixtures or emulated-memory writes are needed. */
#include <nds.h>
#include <stdint.h>
#include <string.h>
#include "render_backend.h"

_Alignas(32) volatile uint32_t g_nds_admission_probe[4];
static dok_nds_render_quad quads[615];
static uint8_t pixels[64];
static uint16_t palette[256];
static uint32_t checks, failures;

static void expect(int value) { ++checks; if (!value) ++failures; }

static dok_nds_render_result submit(dok_nds_render_frame *frame,
                                    dok_nds_render_stats *stats)
{
    dok_nds_render_result result = dok_nds_render_submit(frame, stats);
    if (result == DOK_NDS_RENDER_OK) {
        swiWaitForVBlank();
        dok_nds_render_present();
    }
    return result;
}

int main(void)
{
    uint32_t i;
    dok_nds_render_stats stats, sentinel;
    dok_nds_render_texture texture = {0,pixels,64,palette,8,8,8,8,0};
    dok_nds_render_cell cell = {0,0,0,quads,614};
    dok_nds_render_frame frame = {{0,0,0,0,32768},&cell,1,1};
    expect(dok_nds_render_init() == DOK_NDS_RENDER_OK);
    expect(dok_nds_render_preload(&texture,1,&stats) == DOK_NDS_RENDER_OK);
    for (i=0;i<615u;++i) {
        quads[i].texture_slot=UINT16_MAX;
        quads[i].color_rgb555=32767;
        quads[i].vertex[0]=(dok_nds_render_vertex){0,0,1024,0,0};
        quads[i].vertex[1]=(dok_nds_render_vertex){16,0,1024,0,0};
        quads[i].vertex[2]=(dok_nds_render_vertex){16,16,1024,0,0};
        quads[i].vertex[3]=(dok_nds_render_vertex){0,16,1024,0,0};
    }
    expect(submit(&frame,&stats) == DOK_NDS_RENDER_OK); /* fast <=614 */
    cell.quad_count=615;
    expect(submit(&frame,&stats) == DOK_NDS_RENDER_OK); /* exact interior */
    for (i=0;i<615u;++i) {
        quads[i].vertex[0].x=quads[i].vertex[3].x=-4096;
        quads[i].vertex[1].x=quads[i].vertex[2].x=4095;
    }
    memset(&stats,0xa5,sizeof(stats));sentinel=stats;
    expect(submit(&frame,&stats) == DOK_NDS_RENDER_CAPACITY); /* 615*10 */
    expect(memcmp(&stats,&sentinel,sizeof(stats)) == 0);
    cell.quad_count=614;
    expect(submit(&frame,&stats) == DOK_NDS_RENDER_OK); /* 614*10 */
    /* UV/range admission is mandatory even on the fast branch. */
    cell.quad_count=1;quads[0].vertex[0].u_q16=-1;
    expect(submit(&frame,&stats) == DOK_NDS_RENDER_RANGE);
    quads[0].vertex[0].u_q16=0;quads[0].vertex[0].x=4096;
    expect(submit(&frame,&stats) == DOK_NDS_RENDER_RANGE);
    quads[0].vertex[0].x=0;
    /* Matrix itself fits, but a vertex transform can exceed signed32. */
    cell.origin_x=214748160;quads[0].vertex[1].x=4095;
    expect(submit(&frame,&stats) == DOK_NDS_RENDER_RANGE);
    g_nds_admission_probe[0]=0x5041444bu;
    g_nds_admission_probe[1]=checks;g_nds_admission_probe[2]=failures;
    g_nds_admission_probe[3]=1;
    DC_FlushRange((const void *)g_nds_admission_probe,sizeof(g_nds_admission_probe));
    for (;;) swiWaitForVBlank();
}
