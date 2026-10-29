#include "render_backend.h"

#include <limits.h>
#include <string.h>
#include <nds.h>
#include <nds/arm9/postest.h>

/* Positive-forward N06 projection, one GPU unit=512 world units.
 * near=200world, far=49152world. Off-axis coefficients rounded to Q12;
 * actual GE PosTest checks this adaptation independently of raster output. */
static const int32_t projection[16] = {
    5120, 0, 0, 0,
    0, -10240, 0, 0,
    -13, 26, 4129, 4096,
    0, 0, -3213, 0
};

typedef struct ResidentTexture {
    int sdk_id;
    uint16_t width, height;
} ResidentTexture;

static ResidentTexture resident[DOK_NDS_RENDER_TEXTURE_CAPACITY];
static uint32_t resident_count;
static uint32_t resident_bytes, resident_palette_bytes;
static uint32_t sticky_disp3d, sticky_gxstat;
static int initialized;
static int loading;

static int64_t floor_div(int64_t value, int64_t divisor)
{
    int64_t result = value / divisor;
    if (value % divisor < 0) --result;
    return result;
}

static int32_t nearest_div(int32_t value, int32_t divisor)
{
    return value >= 0 ? (value + divisor / 2) / divisor
                      : -((-value + divisor / 2) / divisor);
}

static dok_nds_render_result fence(uint32_t *iterations)
{
    uint32_t n = 0;
    while (GFX_STATUS & (1u << 27)) {
        if (++n == 0x100000u) return DOK_NDS_RENDER_TIMEOUT;
    }
    if (iterations != NULL) *iterations = n;
    return DOK_NDS_RENDER_OK;
}

void dok_nds_render_observe(dok_nds_render_stats *stats)
{
    uint32_t control = GFX_CONTROL;
    uint32_t gxstat = GFX_STATUS;
    sticky_disp3d |= control & (GL_COLOR_UNDERFLOW | GL_POLY_OVERFLOW);
    sticky_gxstat |= gxstat & (1u << 15);
    if (stats != NULL) {
        stats->disp3d_flags = sticky_disp3d;
        stats->gxstat_flags = sticky_gxstat;
        stats->rdlines = GFX_RDLINES_COUNT & 63u;
        stats->texture_bytes = resident_bytes;
        stats->palette_bytes = resident_palette_bytes;
    }
}

/* Sample sticky flags before every DISP3DCNT write. W1C bits in a read-
 * modify-write must never silently discard an observed overflow. */
static void set_control(uint16_t enabled)
{
    dok_nds_render_observe(NULL);
    GFX_CONTROL = enabled | GL_COLOR_UNDERFLOW | GL_POLY_OVERFLOW;
}

static dok_nds_render_result modelview(
    const dok_nds_render_camera *camera, const dok_nds_render_cell *cell,
    m4x3 *matrix)
{
    int32_t c, s;
    int64_t dx, dz, tx, ty, tz;
    if (camera == NULL || cell == NULL || matrix == NULL)
        return DOK_NDS_RENDER_ARGUMENT;
    if (camera->sin_q15 < -32768 || camera->sin_q15 > 32768 ||
        camera->cos_q15 < -32768 || camera->cos_q15 > 32768 ||
        cell->origin_x % 512 != 0 || cell->origin_y % 512 != 0 ||
        cell->origin_z % 512 != 0)
        return DOK_NDS_RENDER_RANGE;
    c = nearest_div(camera->cos_q15, 8);
    s = nearest_div(camera->sin_q15, 8);
    /* floor, rather than sign-sensitive rounding, gives translation by an
     * integer-lattice cell an exactly linear shift. Shared endpoints use
     * identical final coordinates with the same R_Q12 and origin lattice. */
    dx = (int64_t)cell->origin_x - camera->x;
    dz = (int64_t)cell->origin_z - camera->z;
    tx = floor_div(dx * c + dz * s, 512);
    tz = floor_div(dz * c - dx * s, 512) + 1600;
    ty = ((int64_t)cell->origin_y - camera->y) * 8;
    if (tx < INT32_MIN || tx > INT32_MAX || ty < INT32_MIN ||
        ty > INT32_MAX || tz < INT32_MIN || tz > INT32_MAX)
        return DOK_NDS_RENDER_RANGE;
    *matrix = (m4x3){{c, 0, -s, 0, 4096, 0, s, 0, c,
                     (int32_t)tx, (int32_t)ty, (int32_t)tz}};
    return DOK_NDS_RENDER_OK;
}

/* Ordinary GE projection*modelview matrix composition rounds different
 * translation columns independently and can split a shared endpoint by one
 * clip LSB. Build one quantized linear basis, then translate it by exact
 * integer GPU units. Position matrix stays identity; this combined matrix
 * is loaded as projection. Thus origin changes compensate exactly. */
typedef struct CameraPlan {
    m4x4 basis;
    int32_t anchor_x, anchor_y, anchor_z;
} CameraPlan;

static dok_nds_render_result camera_plan(
    const dok_nds_render_camera *camera, CameraPlan *plan)
{
    dok_nds_render_cell anchor;
    m4x3 camera_model, zero_model;
    dok_nds_render_camera zero_camera = {0, 0, 0, 0, 32768};
    dok_nds_render_cell zero_cell = {0, 0, 0, NULL, 0};
    unsigned row, col, k;
    dok_nds_render_result result;
    if (camera == NULL || plan == NULL) return DOK_NDS_RENDER_ARGUMENT;
    anchor = (dok_nds_render_cell){
        (int32_t)(floor_div(camera->x, 512) * 512),
        (int32_t)(floor_div(camera->y, 512) * 512),
        (int32_t)(floor_div(camera->z, 512) * 512), NULL, 0};
    result = modelview(camera, &anchor, &camera_model);
    if (result != DOK_NDS_RENDER_OK) return result;
    zero_camera.sin_q15 = camera->sin_q15;
    zero_camera.cos_q15 = camera->cos_q15;
    result = modelview(&zero_camera, &zero_cell, &zero_model);
    if (result != DOK_NDS_RENDER_OK) return result;
    plan->anchor_x=anchor.origin_x; plan->anchor_y=anchor.origin_y;
    plan->anchor_z=anchor.origin_z;
    for (col = 0; col < 3; ++col) {
        for (row = 0; row < 4; ++row) {
            int64_t sum = 0;
            for (k = 0; k < 3; ++k)
                sum += (int64_t)projection[k * 4 + row] * zero_model.m[col * 3 + k];
            plan->basis.m[col * 4 + row] = (int)floor_div(sum, 4096);
        }
    }
    for (row = 0; row < 4; ++row) {
        int64_t sum = 0;
        for (k = 0; k < 3; ++k)
            sum += (int64_t)projection[k * 4 + row] * camera_model.m[9 + k];
        plan->basis.m[12 + row] = (int)(floor_div(sum, 4096) + projection[12 + row]);
    }
    return DOK_NDS_RENDER_OK;
}

static dok_nds_render_result planned_cell_clip(
    const CameraPlan *plan, const dok_nds_render_cell *cell, m4x4 *clip)
{
    int64_t shift[3];
    unsigned row, k;
    if (cell->origin_x % 512 || cell->origin_y % 512 || cell->origin_z % 512)
        return DOK_NDS_RENDER_RANGE;
    shift[0]=((int64_t)cell->origin_x-plan->anchor_x)/512;
    shift[1]=((int64_t)cell->origin_y-plan->anchor_y)/512;
    shift[2]=((int64_t)cell->origin_z-plan->anchor_z)/512;
    *clip=plan->basis;
    for (row=0;row<4;++row) {
        int64_t translated=plan->basis.m[12+row];
        for (k=0;k<3;++k)
            translated+=(int64_t)clip->m[k*4+row]*shift[k];
        if (translated<INT32_MIN || translated>INT32_MAX)
            return DOK_NDS_RENDER_RANGE;
        clip->m[12+row]=(int)translated;
    }
    return DOK_NDS_RENDER_OK;
}

static dok_nds_render_result cell_clip(
    const dok_nds_render_camera *camera, const dok_nds_render_cell *cell,
    m4x4 *clip)
{
    CameraPlan plan;
    dok_nds_render_result result=camera_plan(camera,&plan);
    if(result!=DOK_NDS_RENDER_OK) return result;
    return planned_cell_clip(&plan,cell,clip);
}

static void load_cell_clip(const m4x4 *clip)
{
    glMatrixMode(GL_PROJECTION);
    glLoadMatrix4x4(clip);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

static int vertex_valid(const dok_nds_render_vertex *vertex,
                        const ResidentTexture *texture)
{
    if (vertex->x < -4096 || vertex->x > 4095 ||
        vertex->y < -4096 || vertex->y > 4095 ||
        vertex->z < -4096 || vertex->z > 4095)
        return 0;
    /* A padded non-POT resource cannot repeat at its storage boundary. The
     * builder splits at logical UV resets and supplies bounded coordinates. */
    if (vertex->u_q16 < 0 || vertex->v_q16 < 0 ||
        (int64_t)vertex->u_q16 > (int64_t)texture->width * 65536 ||
        (int64_t)vertex->v_q16 > (int64_t)texture->height * 65536)
        return 0;
    return 1;
}

static int vertex_inside(const m4x4 *clip,
                         const dok_nds_render_vertex *vertex)
{
    int64_t out[4];
    unsigned row;
    for (row = 0; row < 4; ++row) {
        int64_t sum = (int64_t)clip->m[row] * vertex->x * 8 +
                      (int64_t)clip->m[4 + row] * vertex->y * 8 +
                      (int64_t)clip->m[8 + row] * vertex->z * 8;
        out[row] = floor_div(sum, 4096) + clip->m[12 + row];
        if (out[row] < INT32_MIN || out[row] > INT32_MAX) return -1;
    }
    /* A generous 4-Q12-step strict margin accommodates GPU clip-matrix
     * multiply truncation. Borderline geometry receives worst-case bound. */
    return out[3] > 4 && out[0] > -out[3] + 4 && out[0] < out[3] - 4 &&
           out[1] > -out[3] + 4 && out[1] < out[3] - 4 &&
           out[2] > -out[3] + 4 && out[2] < out[3] - 4;
}

static int clip_range_bounded(const m4x4 *clip)
{
    unsigned row, col;
    for (row = 0; row < 4; ++row) {
        int64_t bound = 0, translation = clip->m[12 + row];
        /* |local| <= 4096; vertex_inside multiplies it by eight and
         * divides by 4096. Include a floor-rounding unit conservatively. */
        for (col = 0; col < 3; ++col) {
            int64_t coefficient = clip->m[col * 4 + row];
            bound += (coefficient < 0 ? -coefficient : coefficient) * 8;
        }
        if (translation - bound - 1 < INT32_MIN ||
            translation + bound + 1 > INT32_MAX) return 0;
    }
    return 1;
}

dok_nds_render_result dok_nds_render_init(void)
{
    m4x4 sdk_projection;
    unsigned i;
    videoSetMode(MODE_5_3D);
    videoSetModeSub(MODE_5_2D);
    lcdMainOnTop();
    vramSetBankA(VRAM_A_TEXTURE_SLOT0);
    vramSetBankB(VRAM_B_TEXTURE_SLOT1);
    vramSetBankC(VRAM_C_SUB_BG);
    vramSetBankD(VRAM_D_TEXTURE_SLOT3);
    vramSetBankE(VRAM_E_MAIN_BG);
    vramSetBankF(VRAM_F_TEX_PALETTE_SLOT0);
    vramSetBankG(VRAM_G_LCD);
    vramSetBankH(VRAM_H_LCD);
    vramSetBankI(VRAM_I_LCD);
    if (!glInit()) return DOK_NDS_RENDER_HARDWARE;
    /* glInit itself may acknowledge pre-existing boot state; task counters
     * start after initialization and before the first submitted geometry. */
    sticky_disp3d = sticky_gxstat = 0;
    set_control(0);
    glClearColor(0, 0, 0, 31);
    glClearPolyID(63);
    glClearDepth(0x7fff);
    /* DS viewport Y is bottom-origin; LCD rows0..153 require Y38..191. */
    glViewport(0, 38, 255, 191);
    glMatrixMode(GL_PROJECTION);
    for (i = 0; i < 16; ++i) sdk_projection.m[i] = (int)projection[i];
    glLoadMatrix4x4(&sdk_projection);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    resident_count = resident_bytes = resident_palette_bytes = 0;
    initialized = 1;
    loading = 1;
    return fence(NULL);
}

static int pot(uint16_t size)
{
    return size >= 8 && size <= 1024 && (size & (size - 1)) == 0;
}

dok_nds_render_result dok_nds_render_preload(
    const dok_nds_render_texture *textures, uint32_t count,
    dok_nds_render_stats *stats)
{
    uint32_t bytes = 0, palette_bytes = 0;
    uint32_t i, j;
    int ids[DOK_NDS_RENDER_TEXTURE_CAPACITY];
    if (!initialized || textures == NULL || count == 0 || stats == NULL)
        return DOK_NDS_RENDER_ARGUMENT;
    if (count > DOK_NDS_RENDER_TEXTURE_CAPACITY) return DOK_NDS_RENDER_CAPACITY;
    for (i = 0; i < count; ++i) {
        const dok_nds_render_texture *t = &textures[i];
        if (t->pixels == NULL || t->palette == NULL ||
            !pot(t->storage_width) || !pot(t->storage_height) ||
            t->logical_width == 0 || t->logical_height == 0 ||
            t->logical_width > t->storage_width ||
            t->logical_height > t->storage_height ||
            t->bytes != (uint32_t)t->storage_width * t->storage_height ||
            t->color0_transparent > 1u)
            return DOK_NDS_RENDER_TEXTURE;
        if (t->bytes > DOK_NDS_RENDER_TEXTURE_BYTES - bytes)
            return DOK_NDS_RENDER_CAPACITY;
        bytes += t->bytes;
        for (j = 0; j < 256; ++j)
            if (t->palette[j] & 0x8000u) return DOK_NDS_RENDER_TEXTURE;
        for (j = 0; j < i; ++j)
            if (memcmp(t->palette, textures[j].palette, 512) == 0) break;
        if (j == i) palette_bytes += 512;
    }
    if (palette_bytes > 16384u) return DOK_NDS_RENDER_CAPACITY;
    if (fence(NULL) != DOK_NDS_RENDER_OK) return DOK_NDS_RENDER_TIMEOUT;
    /* Explicit loading boundary: stop rendering before any bank remap. */
    dok_nds_render_observe(stats);
    REG_DISPCNT &= ~DISPLAY_BG0_ACTIVE;
    powerOff(POWER_3D_CORE);
    loading = 1;
    set_control(0);
    glResetTextures();
    resident_count = resident_bytes = resident_palette_bytes = 0;
    if (!glGenTextures((int)count, ids)) return DOK_NDS_RENDER_TEXTURE;
    for (i = 0; i < count; ++i) {
        const dok_nds_render_texture *t = &textures[i];
        uint32_t flags = TEXGEN_OFF;
        if (t->color0_transparent) flags |= GL_TEXTURE_COLOR0_TRANSPARENT;
        glBindTexture(0, ids[i]);
        if (!glTexImageNtr2D(GL_RGB256, t->storage_width, t->storage_height,
                            flags, t->pixels, NULL)) return DOK_NDS_RENDER_TEXTURE;
        for (j = 0; j < i; ++j)
            if (memcmp(t->palette, textures[j].palette, 512) == 0) break;
        if (j == i) {
            if (!glColorTableNtr(256, t->palette)) return DOK_NDS_RENDER_TEXTURE;
        } else {
            if (!glAssignColorTable(ids[i], ids[j])) return DOK_NDS_RENDER_TEXTURE;
        }
        resident[i] = (ResidentTexture){ids[i], t->logical_width, t->logical_height};
    }
    resident_count = count;
    resident_bytes = bytes;
    resident_palette_bytes = palette_bytes;
    memset(stats, 0, sizeof(*stats));
    stats->uploads = count;
    stats->upload_bytes = bytes + palette_bytes;
    dok_nds_render_observe(stats);
    set_control(GL_TEXTURE_2D);
    powerOn(POWER_3D_CORE);
    return DOK_NDS_RENDER_OK;
}

void dok_nds_render_present(void)
{
    /* Called after the submitted frame's VBlank swap, never after preload
     * alone: old geometry must not be exposed with the new texture set. */
    if (initialized && loading) {
        REG_DISPCNT |= DISPLAY_BG0_ACTIVE;
        loading = 0;
    }
}

dok_nds_render_result dok_nds_render_submit(
    const dok_nds_render_frame *frame, dok_nds_render_stats *stats)
{
    uint32_t i, j, k, quads = 0, vertex_bound = 0;
    uint32_t bound_quads = 0;
    int worst_case_fits;
    m4x4 clip;
    CameraPlan plan;
    dok_nds_render_result result;
    if (!initialized || resident_count == 0 || frame == NULL || stats == NULL ||
        frame->cells == NULL || frame->cell_count == 0)
        return DOK_NDS_RENDER_ARGUMENT;
    if (frame->cell_count > 4096u) return DOK_NDS_RENDER_CAPACITY;
    /* A clipped convex quad needs at most ten GE vertices. Most scenes fit
     * even this bound: avoid four CPU clip transforms per quad in that case.
     * Larger scenes retain the original exact interior/boundary admission.
     * Still validate every matrix, vertex, UV and material before any FIFO
     * write; this is a proof of capacity, not a trusted-input bypass. */
    for (i = 0; i < frame->cell_count; ++i) {
        if (frame->cells[i].quad_count > DOK_NDS_RENDER_POLYGON_LIMIT - bound_quads)
            return DOK_NDS_RENDER_CAPACITY;
        bound_quads += frame->cells[i].quad_count;
    }
    worst_case_fits = bound_quads <= DOK_NDS_RENDER_VERTEX_LIMIT / 10u;
    result=camera_plan(&frame->camera,&plan);
    if(result!=DOK_NDS_RENDER_OK) return result;
    for (i = 0; i < frame->cell_count; ++i) {
        const dok_nds_render_cell *cell = &frame->cells[i];
        int transform_bounded;
        result = planned_cell_clip(&plan, cell, &clip);
        if (result != DOK_NDS_RENDER_OK) return result;
        transform_bounded = worst_case_fits && clip_range_bounded(&clip);
        if (cell->quad_count != 0 && cell->quads == NULL)
            return DOK_NDS_RENDER_ARGUMENT;
        if (cell->quad_count > DOK_NDS_RENDER_POLYGON_LIMIT - quads)
            return DOK_NDS_RENDER_CAPACITY;
        quads += cell->quad_count;
        for (j = 0; j < cell->quad_count; ++j) {
            const dok_nds_render_quad *q = &cell->quads[j];
            int inside = 1;
            if ((q->texture_slot >= resident_count && q->texture_slot != UINT16_MAX) ||
                q->flags != 0 || q->reserved != 0 || q->color_rgb555 > 32767u)
                return DOK_NDS_RENDER_TEXTURE;
            for (k = 0; k < 4; ++k) {
                const ResidentTexture flat = {0, 1024, 1024};
                const ResidentTexture *texture = q->texture_slot == UINT16_MAX
                    ? &flat : &resident[q->texture_slot];
                if (!vertex_valid(&q->vertex[k], texture))
                    return DOK_NDS_RENDER_RANGE;
                if (!transform_bounded) {
                    int position = vertex_inside(&clip, &q->vertex[k]);
                    if (position < 0) return DOK_NDS_RENDER_RANGE;
                    inside &= position;
                }
            }
            /* Clipping a convex quad by six planes adds at most six vertices.
             * Fully interior quads require four. No capacity truncation. */
            vertex_bound += worst_case_fits ? 10u : inside ? 4u : 10u;
            if (vertex_bound > DOK_NDS_RENDER_VERTEX_LIMIT)
                return DOK_NDS_RENDER_CAPACITY;
        }
    }
    result = fence(NULL);
    if (result != DOK_NDS_RENDER_OK) return result;
    memset(stats, 0, sizeof(*stats));
    stats->input_quads = stats->submitted_quads = quads;
    stats->submitted_vertices = quads * 4;
    dok_nds_render_observe(stats);
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_RENDER_FAR_POLYS |
              POLY_RENDER_1DOT_POLYS);
    /* State is local to this submission: diagnostics may change SDK/GE
     * state between frames. Never reuse a binding across that boundary. */
    uint32_t bound_slot = UINT32_MAX, bound_color = UINT32_MAX;
    for (i = 0; i < frame->cell_count; ++i) {
        const dok_nds_render_cell *cell = &frame->cells[i];
        (void)planned_cell_clip(&plan, cell, &clip);
        if (i == 0u) load_cell_clip(&clip);
        else {
            /* Modelview remains identity for the entire submission. */
            glMatrixMode(GL_PROJECTION);
            glLoadMatrix4x4(&clip);
        }
        glBegin(GL_QUADS);
        for (j = 0; j < cell->quad_count; ++j) {
            const dok_nds_render_quad *q = &cell->quads[j];
            uint32_t color = q->texture_slot == UINT16_MAX
                ? q->color_rgb555 : RGB15(31, 31, 31);
            if (bound_slot != q->texture_slot) {
                glBindTexture(0, q->texture_slot == UINT16_MAX
                    ? 0 : resident[q->texture_slot].sdk_id);
                bound_slot = q->texture_slot;
            }
            if (bound_color != color) { glColor(color); bound_color = color; }
            for (k = 0; k < 4; ++k) {
                const dok_nds_render_vertex *v = &q->vertex[k];
                glTexCoord2t16((t16)nearest_div(v->u_q16, 4096),
                              (t16)nearest_div(v->v_q16, 4096));
                glVertex3v16((v16)(v->x * 8), (v16)(v->y * 8), (v16)(v->z * 8));
            }
        }
        glEnd();
    }
    {
        uint32_t start = cpuGetTiming();
        result = fence(&stats->geometry_wait_iterations);
        stats->geometry_wait_ticks = cpuGetTiming() - start;
    }
    if (result != DOK_NDS_RENDER_OK) return result;
    stats->hardware_polygons = GFX_POLYGON_RAM_USAGE;
    stats->hardware_vertices = GFX_VERTEX_RAM_USAGE;
    dok_nds_render_observe(stats);
    if (stats->disp3d_flags || stats->gxstat_flags ||
        stats->hardware_polygons > DOK_NDS_RENDER_POLYGON_LIMIT ||
        stats->hardware_vertices > DOK_NDS_RENDER_VERTEX_LIMIT)
        return DOK_NDS_RENDER_HARDWARE;
    glFlush(GL_ZBUFFERING);
    return DOK_NDS_RENDER_OK;
}

dok_nds_render_result dok_nds_render_position_test(
    const dok_nds_render_camera *camera, const dok_nds_render_cell *cell,
    const dok_nds_render_vertex *vertex, dok_nds_render_clip *out)
{
    m4x4 clip;
    dok_nds_render_clip candidate;
    uint32_t iterations = 0;
    dok_nds_render_result result;
    if (!initialized || vertex == NULL || out == NULL)
        return DOK_NDS_RENDER_ARGUMENT;
    if (vertex->x < -4096 || vertex->x > 4095 ||
        vertex->y < -4096 || vertex->y > 4095 ||
        vertex->z < -4096 || vertex->z > 4095)
        return DOK_NDS_RENDER_RANGE;
    result = cell_clip(camera, cell, &clip);
    if (result != DOK_NDS_RENDER_OK) return result;
    result = fence(NULL);
    if (result != DOK_NDS_RENDER_OK) return result;
    load_cell_clip(&clip);
    PosTest_Asynch((v16)(vertex->x * 8), (v16)(vertex->y * 8), (v16)(vertex->z * 8));
    result = fence(NULL);
    if (result != DOK_NDS_RENDER_OK) return result;
    while (PosTestBusy())
        if (++iterations == 0x100000u) return DOK_NDS_RENDER_TIMEOUT;
    candidate = (dok_nds_render_clip){PosTestXresult(), PosTestYresult(),
                                    PosTestZresult(), PosTestWresult()};
    *out = candidate;
    return DOK_NDS_RENDER_OK;
}

const int32_t *dok_nds_render_projection(void)
{
    return projection;
}

dok_nds_render_result dok_nds_render_cell_matrix(
    const dok_nds_render_camera *camera, const dok_nds_render_cell *cell,
    int32_t out[12])
{
    m4x3 matrix;
    dok_nds_render_result result;
    unsigned i;
    if (out == NULL) return DOK_NDS_RENDER_ARGUMENT;
    result = modelview(camera, cell, &matrix);
    if (result != DOK_NDS_RENDER_OK) return result;
    for (i = 0; i < 12; ++i) out[i] = matrix.m[i];
    return DOK_NDS_RENDER_OK;
}

dok_nds_render_result dok_nds_render_clip_matrix(int32_t out[16])
{
    int matrix[16];
    unsigned i;
    if (out == NULL) return DOK_NDS_RENDER_ARGUMENT;
    if (fence(NULL) != DOK_NDS_RENDER_OK) return DOK_NDS_RENDER_TIMEOUT;
    glGetFixed(GL_GET_MATRIX_CLIP, matrix);
    for (i = 0; i < 16; ++i) out[i] = matrix[i];
    return DOK_NDS_RENDER_OK;
}
