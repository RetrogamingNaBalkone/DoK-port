/* N07 architecture experiment: real GE matrices, precision and shared seams.
 * No game world/assets and no renderer readiness claim. */
#include <nds.h>
#include <stdint.h>
#include <string.h>
#include "dok/memory_budget.h"
#include "render_backend.h"
#include "render_probe.h"

_Alignas(32) volatile NdsRenderProbe g_nds_render_probe;
_Alignas(32) volatile NdsRenderHistoryRecord g_nds_render_history[NDS_RENDER_HISTORY_CAPACITY];
_Alignas(32) volatile NdsRenderPositionRecord g_nds_render_positions[NDS_RENDER_POSITION_CAPACITY];
static NdsRenderProbe probe;

static uint32_t address(const void *pointer)
{ return (uint32_t)(uintptr_t)pointer; }

static void publish(void)
{
    probe.magic = NDS_RENDER_PROBE_MAGIC;
    probe.abi_version = NDS_RENDER_ABI_VERSION;
    probe.size_bytes = sizeof(probe);
    probe.sequence = g_nds_render_probe.sequence + 1u;
    g_nds_render_probe.sequence=probe.sequence;
    DC_FlushRange((const void *)&g_nds_render_probe.sequence,32);
    memcpy((void *)&g_nds_render_probe, &probe, sizeof(probe));
    DC_FlushRange((const void *)&g_nds_render_probe, sizeof(probe));
    ++g_nds_render_probe.sequence;
    DC_FlushRange((const void *)&g_nds_render_probe.sequence, 32);
}

static int position(uint32_t case_id, const dok_nds_render_camera *camera,
                    int32_t ox, int32_t oy, int32_t oz,
                    int32_t wx, int32_t wy, int32_t wz)
{
    NdsRenderPositionRecord row = {0};
    dok_nds_render_cell cell = {ox, oy, oz, NULL, 0};
    dok_nds_render_vertex vertex = {wx-ox, wy-oy, wz-oz, 0, 0};
    dok_nds_render_clip clip;
    int32_t model[12], combined[16];
    uint32_t index = probe.position_count;
    if (index >= NDS_RENDER_POSITION_CAPACITY ||
        dok_nds_render_position_test(camera, &cell, &vertex, &clip) != DOK_NDS_RENDER_OK ||
        dok_nds_render_cell_matrix(camera, &cell, model) != DOK_NDS_RENDER_OK ||
        dok_nds_render_clip_matrix(combined) != DOK_NDS_RENDER_OK) return 0;
    row.magic=NDS_RENDER_POSITION_MAGIC; row.abi_version=NDS_RENDER_ABI_VERSION;
    row.size_bytes=sizeof(row); row.sequence=1; row.ordinal=index+1; row.case_id=case_id;
    row.origin_x=(uint32_t)ox; row.origin_y=(uint32_t)oy; row.origin_z=(uint32_t)oz;
    row.local_x=(uint32_t)vertex.x; row.local_y=(uint32_t)vertex.y; row.local_z=(uint32_t)vertex.z;
    row.camera_x=(uint32_t)camera->x; row.camera_y=(uint32_t)camera->y;
    row.camera_z=(uint32_t)camera->z; row.sin_q15=(uint32_t)camera->sin_q15;
    row.cos_q15=(uint32_t)camera->cos_q15;
    row.clip_x=(uint32_t)clip.x; row.clip_y=(uint32_t)clip.y;
    row.clip_z=(uint32_t)clip.z; row.clip_w=(uint32_t)clip.w;
    memcpy(&row.modelview_0, model, sizeof(model));
    memcpy(&row.clipmatrix_0, combined, sizeof(combined));
    g_nds_render_positions[index].sequence=row.sequence;
    DC_FlushRange((const void *)&g_nds_render_positions[index].sequence,32);
    memcpy((void *)&g_nds_render_positions[index], &row, sizeof(row));
    DC_FlushRange((const void *)&g_nds_render_positions[index], sizeof(row));
    ++g_nds_render_positions[index].sequence;
    DC_FlushRange((const void *)&g_nds_render_positions[index].sequence, 32);
    ++probe.position_count;
    return 1;
}

int main(void)
{
    static const dok_nds_render_camera cameras[] = {
        {0,-300,0,0,32768}, {0,-300,0,23170,23170},
        {-113,-301,-217,-23170,23170}, {15120,-300,22800,32768,0},
        {20880,-300,3120,32768,0}, {17520,-300,28560,32768,0}
    };
    uint32_t i;
    uint32_t limit=address(getHeapLimit());
    probe.is_dsi=isDSiMode();
    if (limit < DOK_NDS_ARM9_LIMIT ||
        (limit > DOK_NDS_ARM9_LIMIT && reduceHeapSize(limit-DOK_NDS_ARM9_LIMIT) != 0) ||
        reduceHeapSize(DOK_NDS_RESERVE_BYTES) != 0 ||
        dok_nds_render_init() != DOK_NDS_RENDER_OK) probe.error_bits=1;
    probe.position_address=address((const void *)g_nds_render_positions);
    probe.position_record_bytes=sizeof(NdsRenderPositionRecord);
    probe.projection_address=address(dok_nds_render_projection());
    probe.projection_words=16;
    for (i=0; !probe.error_bits && i<sizeof(cameras)/sizeof(cameras[0]); ++i) {
        const dok_nds_render_camera *camera=&cameras[i];
        int32_t ox=(camera->x/512)*512, oz=(camera->z/512)*512;
        int32_t wx=ox-512, wz=oz+2048;
        if (!position(i+1,camera,ox,0,oz,wx,0,wz) ||
            !position(i+1,camera,ox+512,0,oz,wx,0,wz) ||
            !position(i+11,camera,ox,0,oz,ox+240,-951,oz+1440) ||
            !position(i+21,camera,ox,0,oz,ox+240,-561,oz+1440))
            probe.error_bits=2;
    }
    probe.status=probe.error_bits ? NDS_RENDER_FAILED : NDS_RENDER_COMPLETE;
    publish();
    for (;;) swiWaitForVBlank();
}
