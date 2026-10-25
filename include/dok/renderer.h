#ifndef DOK_RENDERER_H
#define DOK_RENDERER_H
#include "dok/rom_assets.h"
#include "dok/projection.h"
#define DOK_RENDER_WIDTH 240
#define DOK_RENDER_HEIGHT 160
#define DOK_RENDER_SCENE_HEIGHT 144
#ifndef DOK_RENDER_FACE_CAPACITY
#define DOK_RENDER_FACE_CAPACITY 256
#endif
/* G05 static subset. No heap, original image payloads remain borrowed in ROM.
 * Frame is indexed240x160; rows144..159 are cleared for caller's message UI. */
typedef struct {
    const uint8_t *pixels;
    uint16_t width,height,flags,resource_id;
} dok_renderer_image;
typedef struct {
    int32_t x0,x1,top0,top1,bottom0,bottom1,depth0,depth1,u0,u1,key;
    uint16_t image_index;
    int16_t v0;
} dok_renderer_face;
typedef struct {
    uint32_t frame_guard_before; /* caller-owned; renderer never writes guards */
    uint8_t frame[DOK_RENDER_WIDTH*DOK_RENDER_HEIGHT];
    uint32_t frame_guard_after;
    dok_renderer_face faces[DOK_RENDER_FACE_CAPACITY];
    dok_renderer_image images[DOK_RUNTIME_MAX_OBJECTS];
    uint8_t image_known[DOK_RUNTIME_MAX_OBJECTS];
    uint8_t light_row[256]; /* selected row0 copied once per frame */
    uint16_t face_count;
} dok_renderer_workspace;
typedef struct {
    uint32_t cells_considered,wall_cells,center_cells,faces_built,faces_drawn;
    uint32_t faces_rejected,near_clipped,pixels_written,transparent_pixels;
    uint32_t skipped_billboards;
    uint16_t face_capacity,face_count;
} dok_renderer_stats;
typedef enum {
    DOK_RENDER_OK,DOK_RENDER_ARGUMENT,DOK_RENDER_PROFILE,DOK_RENDER_CAPACITY,
    DOK_RENDER_RESOURCE,DOK_RENDER_PROJECTION
} dok_renderer_status;
/* Initial cardinal90 camera only in G05. Output untouched on failure. */
dok_renderer_status dok_renderer_camera_from_runtime(dok_projection_camera *out,const dok_runtime *runtime);
/* Camera coordinates may be supplied for controlled probes. Sine32768/cosine0
 * and native lens/centres are the currently supported profile. On failure frame
 * is diagnostic/partial; caller must not present it as a successful scene. */
dok_renderer_status dok_renderer_draw(dok_renderer_workspace *workspace,
    const dok_runtime *runtime,const dok_rom_assets *assets,unsigned level,unsigned language,
    const dok_projection_camera *camera,dok_renderer_stats *stats);
/* Raw VGA6-bit RGB triplets, exactly768 bytes, no palette conversion here. */
dok_renderer_status dok_renderer_palette(uint8_t out[768],const dok_rom_assets *assets,unsigned level,unsigned language);
#endif
