#include "dok/hotcode.h"
#include "render_backend.h"
#include "dok/vram_layout.h"

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
static uint32_t light_palette_rows, light_palette_base;
static uint32_t darkness, applied_darkness;
#ifndef N23_RELATIVE_MATRICES
#define N23_RELATIVE_MATRICES 0
#endif
#if N23_RELATIVE_MATRICES > 1
_Alignas(32) uint32_t g_nds_relative_matrix_probe[32];
#endif
#ifndef N23_SIMPLE_QUADS
#define N23_SIMPLE_QUADS 0
#endif
#if !N23_SIMPLE_QUADS
/* Four2-bit vertex indices and one reuse bit; capacity is unchanged. */
typedef struct StripDraw {uint16_t packed;} StripDraw;
_Static_assert(sizeof(StripDraw)==2u,"bounded strip metadata");
static StripDraw strip_draw[DOK_NDS_RENDER_POLYGON_LIMIT];
#endif
#ifndef N23_CLIP_CACHE
#define N23_CLIP_CACHE 0
#endif
#if N23_CLIP_CACHE
/* Per-submit reuse only: the checked immutable cell matrices stay valid until
 * this call returns. Larger public frames retain the original recomputation. */
static m4x4 checked_clip[64];
#endif
#if !N23_SIMPLE_QUADS
DOK_NDS_HOT_CODE static unsigned strip_order(const StripDraw *d,unsigned k)
{return (d->packed>>(2u*k))&3u;}
DOK_NDS_HOT_CODE static unsigned strip_reuse(const StripDraw *d)
{return (d->packed>>8u)&1u;}
_Static_assert(sizeof(dok_nds_render_vertex)==5u*sizeof(int32_t),"strip vertex has no padding");
DOK_NDS_HOT_CODE static int strip_vertex_equal(const dok_nds_render_vertex *a,const dok_nds_render_vertex *b)
{
    return a->x==b->x && a->y==b->y && a->z==b->z &&
           a->u_q16==b->u_q16 && a->v_q16==b->v_q16;
}
DOK_NDS_HOT_CODE static int shared_strip(const dok_nds_render_quad *a,const StripDraw *ad,
    const dok_nds_render_quad *b,StripDraw *bd)
{
    bd->packed=UINT16_C(0x00b4);
    if(!a || a->texture_slot!=b->texture_slot || a->color_rgb555!=b->color_rgb555)return 0;
    for(uint32_t i=0;i<4u;++i)for(uint32_t reverse=0;reverse<2u;++reverse) {
        uint32_t j=(i+(reverse?3u:1u))%4u;
        if(!strip_vertex_equal(&a->vertex[strip_order(ad,2u)],&b->vertex[i]) ||
           !strip_vertex_equal(&a->vertex[strip_order(ad,3u)],&b->vertex[j]))continue;
        bd->packed=(uint16_t)(i|(j<<2u)|(((i+(reverse?1u:3u))%4u)<<4u)|
            (((i+2u)%4u)<<6u)|256u);return 1;
    }
    return 0;
}
#endif

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

DOK_NDS_HOT_CODE static dok_nds_render_result fence(uint32_t *iterations)
{
    uint32_t n = 0;
    while (GFX_STATUS & (1u << 27)) {
        if (++n == 0x100000u) return DOK_NDS_RENDER_TIMEOUT;
    }
    if (iterations != NULL) *iterations = n;
    return DOK_NDS_RENDER_OK;
}

DOK_NDS_HOT_CODE void dok_nds_render_observe(dok_nds_render_stats *stats)
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
    GFX_CONTROL = enabled | (applied_darkness ? GL_FOG | (4u << 8) : 0u) |
        GL_COLOR_UNDERFLOW | GL_POLY_OVERFLOW;
}

void dok_nds_render_darkness(int enabled)
{
    darkness=enabled!=0;
}
DOK_NDS_HOT_CODE static void commit_darkness(void)
{
    if(darkness==applied_darkness)return;
    /* Z-buffer mode and the accepted Q12 projection are retained. At a fog
     * boundary z15, invert z_ndc=(4129-3213*512/depth)/4096.
     * 64 z15 steps cover the useful 1920..3720-world darkness transition.
     * GBATEK boundaries are offset+step*(i+1), not offset+step*i. */
    glFogOffset(29248);
    glFogColor(0,0,0,31);
    for(uint32_t i=0;i<32u;++i) {
        int64_t z=29248+64*(i+1u);
        int64_t denominator=8225*32767-8192*z;
        int64_t depth=denominator>0 ? (int64_t)3213*512*32767/denominator : 49152;
        int64_t density=(depth-1920)*127/1800;
        glFogDensity((int)i,(int)(density<0?0:density>127?127:density));
    }
    applied_darkness=darkness;set_control(GL_TEXTURE_2D);
}

DOK_NDS_HOT_CODE static dok_nds_render_result modelview(
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

DOK_NDS_HOT_CODE static dok_nds_render_result camera_plan(
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

DOK_NDS_HOT_CODE static dok_nds_render_result planned_cell_clip(
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

DOK_NDS_HOT_CODE static void load_cell_clip(const m4x4 *clip)
{
    glMatrixMode(GL_PROJECTION);
    glLoadMatrix4x4(clip);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

DOK_NDS_HOT_CODE static int vertex_valid(const dok_nds_render_vertex *vertex,
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

DOK_NDS_HOT_CODE static int clip_range_bounded(const m4x4 *clip)
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
    light_palette_rows=0;
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

#if N23_PIPELINE_PROFILE
/* Diagnostic-only split of validation and FIFO emission. No omitted work. */
_Alignas(32) uint32_t g_nds_n23_submit_profile[8+8*16];
#endif
DOK_NDS_HOT_CODE dok_nds_render_result dok_nds_render_submit(
    const dok_nds_render_frame *frame, dok_nds_render_stats *stats)
{
#if N23_PIPELINE_PROFILE
    uint32_t pipeline_start=cpuGetTiming(),pipeline_camera,pipeline_validated;
    uint32_t pipeline_fenced,pipeline_emitted,pipeline_completed;
#endif
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
#if N23_PIPELINE_PROFILE
    pipeline_camera=cpuGetTiming();
#endif
    for (i = 0; i < frame->cell_count; ++i) {
        const dok_nds_render_cell *cell = &frame->cells[i];
#if !N23_SIMPLE_QUADS
        const dok_nds_render_quad *previous_quad=NULL;StripDraw *previous_draw=NULL;int previous_inside=0;
#endif
        int transform_bounded;
        result = planned_cell_clip(&plan, cell, &clip);
        if (result != DOK_NDS_RENDER_OK) return result;
#if N23_CLIP_CACHE
        if(frame->cell_count<=64u)checked_clip[i]=clip;
#endif
        transform_bounded = worst_case_fits && clip_range_bounded(&clip);
        if (cell->quad_count != 0 && cell->quads == NULL)
            return DOK_NDS_RENDER_ARGUMENT;
        if (cell->quad_count > DOK_NDS_RENDER_POLYGON_LIMIT - quads)
            return DOK_NDS_RENDER_CAPACITY;
        quads += cell->quad_count;
        for (j = 0; j < cell->quad_count; ++j) {
            const dok_nds_render_quad *q = &cell->quads[j];
            int inside = 1;
            uint32_t outside_any=0,outside_all=63u;
            if ((q->texture_slot >= resident_count && q->texture_slot != UINT16_MAX) ||
                q->flags != 0 || (light_palette_rows?q->reserved>=light_palette_rows:q->reserved!=0) || q->color_rgb555 > 32767u)
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
                    uint32_t mask=0,strict=0;int64_t vclip[4];
                    for(uint32_t r=0;r<4u;++r) {
                        int64_t sum=(int64_t)clip.m[r]*q->vertex[k].x*8+
                            (int64_t)clip.m[4+r]*q->vertex[k].y*8+
                            (int64_t)clip.m[8+r]*q->vertex[k].z*8;
                        vclip[r]=floor_div(sum,4096)+clip.m[12+r];
                    }
                    for(uint32_t r=0;r<3u;++r) {
                        if(vclip[r]<-vclip[3]+4)mask|=1u<<(r*2u);
                        if(vclip[r]>vclip[3]-4)mask|=2u<<(r*2u);
                    }
                    for(uint32_t r=0;r<3u;++r) {
                        if(vclip[r]<-vclip[3]-4)strict|=1u<<(r*2u);
                        if(vclip[r]>vclip[3]+4)strict|=2u<<(r*2u);
                    }
                    outside_any|=mask;outside_all&=strict;
                }
            }
            /* Clipping a convex quad by six planes adds at most six vertices.
             * Fully interior quads require four. No capacity truncation. */
#if !N23_SIMPLE_QUADS
            StripDraw *draw=&strip_draw[quads-cell->quad_count+j];
            shared_strip(previous_inside && inside?previous_quad:NULL,previous_draw,q,draw);
            vertex_bound += worst_case_fits ? 10u : inside ? 4u-2u*strip_reuse(draw) : outside_all?0u:4u+(uint32_t)__builtin_popcount(outside_any);
            previous_quad=q;previous_draw=draw;previous_inside=inside;
#else
            vertex_bound += worst_case_fits ? 10u : inside ? 4u : outside_all?0u:4u+(uint32_t)__builtin_popcount(outside_any);
#endif
            if (vertex_bound > DOK_NDS_RENDER_VERTEX_LIMIT)
                return DOK_NDS_RENDER_CAPACITY;
        }
    }
#if N23_PIPELINE_PROFILE
    pipeline_validated=cpuGetTiming();
#endif
    result = fence(NULL);
    if (result != DOK_NDS_RENDER_OK) return result;
#if N23_PIPELINE_PROFILE
    pipeline_fenced=cpuGetTiming();
#endif
    memset(stats, 0, sizeof(*stats));
    stats->input_quads = stats->submitted_quads = quads;
    stats->submitted_vertices = quads * 4;
    dok_nds_render_observe(stats);
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_RENDER_FAR_POLYS |
              POLY_RENDER_1DOT_POLYS | (darkness ? POLY_FOG : 0));
    /* State is local to this submission: diagnostics may change SDK/GE
     * state between frames. Never reuse a binding across that boundary. */
    uint32_t bound_slot = UINT32_MAX, bound_color = UINT32_MAX, bound_palette=UINT32_MAX;
    uint32_t submitted_vertices=0;
#if !N23_SIMPLE_QUADS
    uint32_t draw_index=0;
#endif
    for (i = 0; i < frame->cell_count; ++i) {
        const dok_nds_render_cell *cell = &frame->cells[i];
#if N23_RELATIVE_MATRICES
        int translated=0;
        if(i){
            const dok_nds_render_cell *previous=&frame->cells[i-1u];
            /* First-pass admission already proved aligned origins and every
             * absolute clip matrix. World units /512 become Q12 via *8. */
            int64_t dx=((int64_t)cell->origin_x-previous->origin_x)*8;
            int64_t dy=((int64_t)cell->origin_y-previous->origin_y)*8;
            int64_t dz=((int64_t)cell->origin_z-previous->origin_z)*8;
            if(dx>=INT32_MIN&&dx<=INT32_MAX&&dy>=INT32_MIN&&dy<=INT32_MAX&&dz>=INT32_MIN&&dz<=INT32_MAX){
                glTranslatef32((int32_t)dx,(int32_t)dy,(int32_t)dz);translated=1;
            }
        }
        if(!translated){
            (void)planned_cell_clip(&plan,cell,&clip);
            if(!i){load_cell_clip(&clip);glMatrixMode(GL_PROJECTION);}
            else glLoadMatrix4x4(&clip);
        }
#if N23_RELATIVE_MATRICES > 1
        m4x4 actual_clip,expected_clip;
        if(fence(NULL)!=DOK_NDS_RENDER_OK||planned_cell_clip(&plan,cell,&expected_clip)!=DOK_NDS_RENDER_OK)return DOK_NDS_RENDER_HARDWARE;
        glGetFixed(GL_GET_MATRIX_CLIP,actual_clip.m);
        g_nds_relative_matrix_probe[0]=0x234d4b44u;g_nds_relative_matrix_probe[1]=1;
        ++g_nds_relative_matrix_probe[2];
        ++g_nds_relative_matrix_probe[translated?4u:5u];
        if(memcmp(&actual_clip,&expected_clip,sizeof(actual_clip))){
            ++g_nds_relative_matrix_probe[3];
            DC_FlushRange(g_nds_relative_matrix_probe,sizeof(g_nds_relative_matrix_probe));
            return DOK_NDS_RENDER_HARDWARE;
        }
        DC_FlushRange(g_nds_relative_matrix_probe,sizeof(g_nds_relative_matrix_probe));
#endif
#else
#if N23_CLIP_CACHE
        if(frame->cell_count<=64u)clip=checked_clip[i];
        else
#endif
        (void)planned_cell_clip(&plan, cell, &clip);
        if (i == 0u) load_cell_clip(&clip);
        else {
            /* Modelview remains identity for the entire submission. */
            glMatrixMode(GL_PROJECTION);
            glLoadMatrix4x4(&clip);
        }
#endif
#if N23_SIMPLE_QUADS
        glBegin(GL_QUADS);
#endif
        for (j = 0; j < cell->quad_count; ++j) {
            const dok_nds_render_quad *q = &cell->quads[j];
#if !N23_SIMPLE_QUADS
            const StripDraw *draw=&strip_draw[draw_index++];
            if(!strip_reuse(draw)){if(j)glEnd();glBegin(GL_QUAD_STRIP);}
#endif
            uint32_t color = q->texture_slot == UINT16_MAX
                ? q->color_rgb555 : RGB15(31, 31, 31);
            if (bound_slot != q->texture_slot) {
                glBindTexture(0, q->texture_slot == UINT16_MAX
                    ? 0 : resident[q->texture_slot].sdk_id);
                bound_slot = q->texture_slot;
                bound_palette=UINT32_MAX;
            }
            uint32_t palette_row=darkness?0u:q->reserved;
            if(light_palette_rows && q->texture_slot!=UINT16_MAX && bound_palette!=palette_row) {
                GFX_PAL_FORMAT=light_palette_base+palette_row*32u;
                bound_palette=palette_row;
            }
            if (bound_color != color) { glColor(color); bound_color = color; }
#if !N23_SIMPLE_QUADS
            for (k = strip_reuse(draw)?2u:0u; k < 4; ++k) {
                const dok_nds_render_vertex *v = &q->vertex[strip_order(draw,k)];
#else
            for (k = 0; k < 4; ++k) {
                const dok_nds_render_vertex *v = &q->vertex[k];
#endif
                glTexCoord2t16((t16)nearest_div(v->u_q16, 4096),
                              (t16)nearest_div(v->v_q16, 4096));
                glVertex3v16((v16)(v->x * 8), (v16)(v->y * 8), (v16)(v->z * 8));
                ++submitted_vertices;
            }
        }
        glEnd();
    }
    stats->submitted_vertices=submitted_vertices;
#if N23_PIPELINE_PROFILE
    pipeline_emitted=cpuGetTiming();
#endif
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
#if N23_PIPELINE_PROFILE
    pipeline_completed=cpuGetTiming();
    uint32_t total=g_nds_n23_submit_profile[2]+1u;
    uint32_t *record=g_nds_n23_submit_profile+8+((total-1u)%8u)*16;
    uint32_t sequence=record[14]+1u;
    record[14]=sequence;DC_FlushRange(record+8,32);
    uint32_t sample[16]={total,pipeline_camera-pipeline_start,
        pipeline_validated-pipeline_camera,pipeline_fenced-pipeline_validated,
        pipeline_emitted-pipeline_fenced,pipeline_completed-pipeline_emitted,
        stats->geometry_wait_ticks,frame->cell_count,quads,submitted_vertices,
        (uint32_t)worst_case_fits,vertex_bound,(uint32_t)frame->camera.x,
        (uint32_t)frame->camera.z,sequence,0};
    memcpy(record,sample,sizeof(sample));
    DC_FlushRange(record,sizeof(sample));
    ++record[14];DC_FlushRange(record+8,32);
    g_nds_n23_submit_profile[0]=0x23534b44u;g_nds_n23_submit_profile[1]=1;
    g_nds_n23_submit_profile[2]=total;g_nds_n23_submit_profile[3]=33513982u;
    DC_FlushRange(g_nds_n23_submit_profile,sizeof(g_nds_n23_submit_profile));
#endif
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

typedef struct StreamSlot {
    uint64_t key;
    dok_nds_render_texture texture;
    uint32_t valid, pinned, pending;
    int sdk_id;
} StreamSlot;
static StreamSlot stream_slots[DOK_NDS_RENDER_TEXTURE_CAPACITY];
static const uint16_t *stream_palette;
static uint32_t stream_colors, stream_palette_pending, stream_planned;
static int stream_palette_id;
static uint32_t stream_repacking;
#ifndef N22_STREAM_LARGEST_MISSES
#define N22_STREAM_LARGEST_MISSES 0
#endif
#ifndef N22_STREAM_PARTIAL_DEFRAG
#define N22_STREAM_PARTIAL_DEFRAG 0
#endif

dok_nds_render_result dok_nds_stream_palette_dirty(void)
{
    uint32_t i;
    if(!stream_palette || stream_planned)return DOK_NDS_RENDER_ARGUMENT;
    for(i=0;i<stream_colors;++i)if(stream_palette[i]&0x8000u)return DOK_NDS_RENDER_TEXTURE;
    stream_palette_pending=1;return DOK_NDS_RENDER_OK;
}

dok_nds_render_result dok_nds_stream_reset(const uint16_t *palette,uint32_t colors)
{
    uint32_t i;
    if(!initialized || !palette || (colors!=256u && colors!=4096u))
        return DOK_NDS_RENDER_ARGUMENT;
    for(i=0;i<colors;++i) if(palette[i]&0x8000u) return DOK_NDS_RENDER_TEXTURE;
    if(fence(NULL)!=DOK_NDS_RENDER_OK) return DOK_NDS_RENDER_TIMEOUT;
    if(!stream_repacking) {
        REG_DISPCNT&=~DISPLAY_BG0_ACTIVE;powerOff(POWER_3D_CORE);loading=1;
    }
    glResetTextures();memset(stream_slots,0,sizeof(stream_slots));
    stream_palette=palette;stream_colors=colors;stream_palette_pending=1;stream_planned=0;
    if(!glGenTextures(1,&stream_palette_id)) return DOK_NDS_RENDER_TEXTURE;
    glBindTexture(0,stream_palette_id);
    if(!glColorTableNtr(colors,NULL)) return DOK_NDS_RENDER_TEXTURE;
    uintptr_t palette_pointer=(uintptr_t)glGetColorTablePointer(stream_palette_id);
    if(palette_pointer<0x06890000u || palette_pointer+colors*2u>0x06894000u)
        return DOK_NDS_RENDER_TEXTURE;
    light_palette_base=(uint32_t)(palette_pointer-0x06890000u)/16u;
    light_palette_rows=colors==4096u?16u:0u;
    resident_count=resident_bytes=0;resident_palette_bytes=colors*2u;
    return DOK_NDS_RENDER_OK;
}
static int stream_pointer_valid(void *pointer,uint32_t bytes)
{
    uintptr_t p=(uintptr_t)pointer;
    return (p>=0x06800000u && p+bytes<=0x06840000u) ||
           (p>=0x06860000u && p+bytes<=0x06880000u);
}
static int stream_allocate(StreamSlot *s,const dok_nds_stream_texture *t)
{
    uint32_t flags=TEXGEN_OFF;
    if(t->texture.color0_transparent) flags|=GL_TEXTURE_COLOR0_TRANSPARENT;
    if(!glGenTextures(1,&s->sdk_id)) return 0;
    glBindTexture(0,s->sdk_id);
    if(!glTexImageNtr2D(GL_RGB256,t->texture.storage_width,t->texture.storage_height,flags,NULL,NULL) ||
       !glAssignColorTable(s->sdk_id,stream_palette_id) ||
       !stream_pointer_valid(glGetTexturePointer(s->sdk_id),t->texture.bytes)) {
        glDeleteTextures(1,&s->sdk_id);return 0;
    }
    s->key=t->key;s->texture=t->texture;s->valid=s->pinned=s->pending=1;return 1;
}
#if N22_STREAM_LARGEST_MISSES || N22_STREAM_PARTIAL_DEFRAG
static int stream_allocate_missing(const dok_nds_stream_texture *textures,uint32_t count)
{
    uint16_t order[DOK_NDS_RENDER_TEXTURE_CAPACITY];uint32_t missing=0,j;
    for(uint32_t i=0;i<count;++i){
        for(j=0;j<DOK_NDS_RENDER_TEXTURE_CAPACITY;++j)
            if(stream_slots[j].valid&&stream_slots[j].key==textures[i].key)break;
        if(j==DOK_NDS_RENDER_TEXTURE_CAPACITY)order[missing++]=(uint16_t)i;
        else resident[i]=(ResidentTexture){stream_slots[j].sdk_id,textures[i].texture.logical_width,textures[i].texture.logical_height};
    }
    for(uint32_t n=0;n<missing;++n){
        uint32_t largest=n;
        for(uint32_t k=n+1u;k<missing;++k)
            if(textures[order[k]].texture.bytes>textures[order[largest]].texture.bytes)largest=k;
        uint16_t temp=order[n];order[n]=order[largest];order[largest]=temp;
    }
    for(uint32_t n=0;n<missing;++n){
        uint32_t request=order[n];
        for(j=0;j<DOK_NDS_RENDER_TEXTURE_CAPACITY;++j)if(!stream_slots[j].valid)break;
        if(j==DOK_NDS_RENDER_TEXTURE_CAPACITY||!stream_allocate(&stream_slots[j],&textures[request]))return 0;
        resident[request]=(ResidentTexture){stream_slots[j].sdk_id,textures[request].texture.logical_width,textures[request].texture.logical_height};
    }
    return 1;
}
#endif
#if N22_STREAM_PARTIAL_DEFRAG
static int stream_partial_relocation(const dok_nds_stream_texture *textures,
    uint32_t count,dok_nds_stream_stats *stats)
{
    _Static_assert(DOK_NDS_RENDER_TEXTURE_CAPACITY==DOK_VRAM_LAYOUT_SLOTS,"planner capacity");
    dok_vram_span spans[DOK_VRAM_LAYOUT_SLOTS];uint32_t missing[DOK_VRAM_LAYOUT_SLOTS];
    uint32_t nspans=0,nmissing=0;
    for(uint32_t i=0;i<DOK_NDS_RENDER_TEXTURE_CAPACITY;++i){const StreamSlot *s=&stream_slots[i];
        if(!s->valid)continue;
        void *pointer=glGetTexturePointer(s->sdk_id);
        if(!stream_pointer_valid(pointer,s->texture.bytes))return 0;
        spans[nspans++]=(dok_vram_span){(uint32_t)(uintptr_t)pointer,s->texture.bytes,s->pending?0u:s->texture.bytes,i};
    }
    for(uint32_t i=0;i<count;++i){uint32_t j=0;
        for(;j<DOK_NDS_RENDER_TEXTURE_CAPACITY;++j)
            if(stream_slots[j].valid&&stream_slots[j].key==textures[i].key)break;
        if(j==DOK_NDS_RENDER_TEXTURE_CAPACITY)missing[nmissing++]=textures[i].texture.bytes;
    }
    dok_vram_relocation plan;
    if(!dok_vram_relocation_plan(spans,nspans,missing,nmissing,&plan)||!plan.count)return 0;
    for(uint32_t i=0;i<DOK_NDS_RENDER_TEXTURE_CAPACITY;++i)
        if(plan.evict[i/8u]&(1u<<(i%8u))){StreamSlot *s=&stream_slots[i];
            glDeleteTextures(1,&s->sdk_id);s->valid=s->pinned=s->pending=0;++stats->evictions;
        }
    ++stats->rebuilds;return 1;
}
#endif
dok_nds_render_result dok_nds_stream_plan(const dok_nds_stream_texture *textures,
    uint32_t count,dok_nds_stream_stats *stats)
{
    uint32_t i,j,total=0,used=0;dok_nds_stream_stats result={0};
    if(!initialized || !stream_palette || !textures || !stats || !count || stream_planned)
        return DOK_NDS_RENDER_ARGUMENT;
    if(count>DOK_NDS_RENDER_TEXTURE_CAPACITY) return DOK_NDS_RENDER_CAPACITY;
    for(i=0;i<count;++i) {
        const dok_nds_render_texture *t=&textures[i].texture;
        if(!textures[i].key || !t->pixels || !pot(t->storage_width) || !pot(t->storage_height) ||
           !t->logical_width || !t->logical_height || t->logical_width>t->storage_width ||
           t->logical_height>t->storage_height || t->bytes!=(uint32_t)t->storage_width*t->storage_height ||
           t->color0_transparent>1u) return DOK_NDS_RENDER_TEXTURE;
        if(t->bytes>DOK_NDS_RENDER_TEXTURE_BYTES-total) return DOK_NDS_RENDER_CAPACITY;
        total+=t->bytes;
        for(j=0;j<i;++j) if(textures[j].key==textures[i].key) return DOK_NDS_RENDER_ARGUMENT;
    }
    if(fence(NULL)!=DOK_NDS_RENDER_OK) return DOK_NDS_RENDER_TIMEOUT;
    for(i=0;i<DOK_NDS_RENDER_TEXTURE_CAPACITY;++i) {
        StreamSlot *s=&stream_slots[i];s->pinned=0;
        if(!s->valid) continue;
        for(j=0;j<count;++j) if(s->key==textures[j].key) break;
        if(j<count) { s->pinned=1;++result.hits; }
        else { glDeleteTextures(1,&s->sdk_id);s->valid=0;++result.evictions; }
    }
#if N22_STREAM_LARGEST_MISSES || N22_STREAM_PARTIAL_DEFRAG
    /* Keep resident allocations in place. New requests alone are sorted, so
     * a small miss cannot consume the only span needed by a later large one. */
    i=stream_allocate_missing(textures,count)?count:0u;
#if N22_STREAM_PARTIAL_DEFRAG
    for(uint32_t attempt=0;i<count&&attempt<4u;++attempt){
        if(!stream_partial_relocation(textures,count,&result))break;
        i=stream_allocate_missing(textures,count)?count:0u;
    }
#endif
#else
    for(i=0;i<count;++i) {
        for(j=0;j<DOK_NDS_RENDER_TEXTURE_CAPACITY;++j)
            if(stream_slots[j].valid && stream_slots[j].key==textures[i].key) break;
        if(j==DOK_NDS_RENDER_TEXTURE_CAPACITY) {
            for(j=0;j<DOK_NDS_RENDER_TEXTURE_CAPACITY;++j)if(!stream_slots[j].valid)break;
            if(j==DOK_NDS_RENDER_TEXTURE_CAPACITY || !stream_allocate(&stream_slots[j],&textures[i])) break;
            ++result.misses;
        }
        resident[i]=(ResidentTexture){stream_slots[j].sdk_id,textures[i].texture.logical_width,textures[i].texture.logical_height};
    }
#endif
    if(i<count) {
#if N22_STREAM_PARTIAL_DEFRAG
        /* A normal frame cannot turn a failed bounded relocation into the
         * old all-textures upload. Its proven DMA deadline remains binding. */
        if(!loading)return DOK_NDS_RENDER_TEXTURE;
#endif
        /* Fragmentation recovery: plan largest allocations first, still without
         * touching VRAM bytes referenced by the currently displayed frame. */
        const uint16_t *palette=stream_palette;uint32_t colors=stream_colors;
        stream_repacking=1;
        dok_nds_render_result reset_result=dok_nds_stream_reset(palette,colors);
        stream_repacking=0;
        if(reset_result!=DOK_NDS_RENDER_OK) return DOK_NDS_RENDER_TEXTURE;
        ++result.rebuilds;result.hits=0;result.misses=count;
        for(i=0;i<count;++i) {
            uint32_t largest=UINT32_MAX;
            for(j=0;j<count;++j) {
                uint32_t k;
                for(k=0;k<i;++k)if(stream_slots[k].key==textures[j].key)break;
                if(k<i)continue;
                if(largest==UINT32_MAX || textures[j].texture.bytes>textures[largest].texture.bytes)largest=j;
            }
            if(largest==UINT32_MAX || !stream_allocate(&stream_slots[i],&textures[largest]))
                return DOK_NDS_RENDER_TEXTURE;
            resident[largest]=(ResidentTexture){stream_slots[i].sdk_id,textures[largest].texture.logical_width,textures[largest].texture.logical_height};
        }
    }
    for(i=0;i<DOK_NDS_RENDER_TEXTURE_CAPACITY;++i) if(stream_slots[i].valid) {
        used+=stream_slots[i].texture.bytes;++result.resident_count;
        if(stream_slots[i].pending) { ++result.uploads;result.upload_bytes+=stream_slots[i].texture.bytes; }
    }
    if(stream_palette_pending) result.upload_bytes+=stream_colors*2u+(stream_colors==4096u?512u:0u);
#if N22_STREAM_LARGEST_MISSES || N22_STREAM_PARTIAL_DEFRAG
    result.misses=result.uploads;result.hits=count-result.misses;
#endif
    result.resident_bytes=resident_bytes=used;resident_count=count;
    stream_planned=1;*stats=result;return DOK_NDS_RENDER_OK;
}
dok_nds_render_result dok_nds_stream_prepare_uploads(void)
{
    if(!stream_planned)return DOK_NDS_RENDER_ARGUMENT;
    for(uint32_t i=0;i<DOK_NDS_RENDER_TEXTURE_CAPACITY;++i) {
        StreamSlot *s=&stream_slots[i];
        if(s->valid&&s->pending)DC_FlushRange(s->texture.pixels,s->texture.bytes);
    }
    if(stream_palette_pending)DC_FlushRange(stream_palette,stream_colors*2u);
    return DOK_NDS_RENDER_OK;
}

static dok_nds_render_result stream_commit(dok_nds_stream_stats *stats,uint32_t clean_sources)
{
    uint32_t i,old;
    if(!stats || !stream_planned) return DOK_NDS_RENDER_ARGUMENT;
    stats->commit_start_line=REG_VCOUNT;
    if(!loading && REG_VCOUNT>=48 && REG_VCOUNT<192) return DOK_NDS_RENDER_HARDWARE;
    commit_darkness();
    old=vramSetPrimaryBanks(VRAM_A_LCD,VRAM_B_LCD,VRAM_C_SUB_BG,VRAM_D_LCD);
    for(i=0;i<DOK_NDS_RENDER_TEXTURE_CAPACITY;++i) {
        StreamSlot *s=&stream_slots[i];
        if(s->valid && s->pending) {
            if(clean_sources)DC_FlushRange(s->texture.pixels,s->texture.bytes);
            dmaCopy(s->texture.pixels,glGetTexturePointer(s->sdk_id),s->texture.bytes);s->pending=0;
        }
    }
    vramRestorePrimaryBanks(old);
    if(stream_palette_pending) {
        uint8_t oldf=VRAM_F_CR;vramSetBankF(VRAM_F_LCD);
        if(clean_sources)DC_FlushRange(stream_palette,stream_colors*2u);
        dmaCopy(stream_palette,glGetColorTablePointer(stream_palette_id),stream_colors*2u);
        if(stream_colors==4096u)dmaCopy(stream_palette,BG_PALETTE,512u);
        VRAM_F_CR=oldf;stream_palette_pending=0;
    }
    stats->commit_end_line=REG_VCOUNT;stream_planned=0;
    if(!loading && REG_VCOUNT>=48 && REG_VCOUNT<192) return DOK_NDS_RENDER_HARDWARE;
    if(loading)powerOn(POWER_3D_CORE);
    set_control(GL_TEXTURE_2D);return DOK_NDS_RENDER_OK;
}

dok_nds_render_result dok_nds_stream_commit(dok_nds_stream_stats *stats)
{return stream_commit(stats,1u);}
dok_nds_render_result dok_nds_stream_commit_prepared(dok_nds_stream_stats *stats)
{return stream_commit(stats,0u);}
