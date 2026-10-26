#ifndef DOK_RENDERER_H
#define DOK_RENDERER_H
#include "dok/rom_assets.h"
#include "dok/projection.h"
#include "dok/image.h"
#define DOK_RENDER_WIDTH 240
#define DOK_RENDER_HEIGHT 160
#define DOK_RENDER_SCENE_HEIGHT 144
#ifndef DOK_RENDER_FACE_CAPACITY
#define DOK_RENDER_FACE_CAPACITY 256
#endif
/* G10 bounded scene renderer. No heap, original image payloads remain borrowed in ROM.
 * Frame is indexed240x160; rows144..159 are cleared for caller's message UI. */
typedef struct {
    const uint8_t *pixels,*columns; /* optional lossless prelit column bank */
    const uint32_t *scales; /* optional ROM v0/span/column tables, exact Q16 */
    uint16_t width,height,flags,resource_id;
} dok_renderer_image;
typedef struct {
    int32_t x0,x1,top0,top1,bottom0,bottom1,depth0,depth1,u0,u1,key;
    uint16_t image_index;
    int16_t v0;
} dok_renderer_face;
/* G10 keeps the four native horizontal vertices, not a raycast floor. */
#define DOK_RENDER_SURFACE_CAPACITY 512
typedef struct {
    int32_t x[4],y[4],depth[4],key;
    uint16_t object,plane,cell_x,cell_z;
    int16_t u0,u1,v0,reserved;
} dok_renderer_surface;
#define DOK_RENDER_SPRITE_CAPACITY 16
typedef struct {int32_t x,y,z;uint16_t object,reserved;} dok_renderer_sprite;
typedef struct {
    uint32_t frame_guard_before; /* caller-owned; renderer never writes guards */
    union {
        uint8_t frame[DOK_RENDER_WIDTH*DOK_RENDER_HEIGHT];
        uint32_t frame_words[DOK_RENDER_WIDTH*DOK_RENDER_HEIGHT/4];
    };
    uint32_t frame_guard_after;
    dok_renderer_face faces[DOK_RENDER_FACE_CAPACITY];
    dok_renderer_image images[DOK_RUNTIME_MAX_OBJECTS];
    uint8_t image_known[DOK_RUNTIME_MAX_OBJECTS];
    uint16_t face_count;
    uint8_t covered_count[240];
    uint8_t covered[240][72][2]; /* normalized disjoint inclusive y intervals */
    dok_renderer_surface surfaces[DOK_RENDER_SURFACE_CAPACITY];
    dok_image_view surface_images[DOK_RUNTIME_MAX_OBJECTS][3];
    uint8_t surface_known[DOK_RUNTIME_MAX_OBJECTS][3];
    uint16_t surface_order[DOK_RENDER_SURFACE_CAPACITY],surface_count;
    uint8_t lights[4096];
    /* Caller-owned positioned instances (projectiles/effects). Borrow object
     * descriptors and ROM resources; no allocation and no gameplay mutation. */
    dok_renderer_sprite sprites[DOK_RENDER_SPRITE_CAPACITY];
    uint16_t sprite_count;
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
/* Integer degrees normalized modulo360, G04 truncation table. */
void dok_renderer_camera_heading(dok_projection_camera *camera,int heading);
/* Native word subtraction/IDIV/clamp, after depth's SAR16. */
unsigned dok_renderer_light_row(int32_t depth,int16_t near);
/* Camera from current runtime pose. Output untouched on failure. */
dok_renderer_status dok_renderer_camera_from_runtime(dok_projection_camera *out,const dok_runtime *runtime);
/* Camera coordinates and Q15 rotation may be supplied for controlled probes.
 * Native lens/centres remain fixed. On failure frame
 * is diagnostic/partial; caller must not present it as a successful scene. */
dok_renderer_status dok_renderer_draw(dok_renderer_workspace *workspace,
    const dok_runtime *runtime,const dok_rom_assets *assets,unsigned level,unsigned language,
    const dok_projection_camera *camera,dok_renderer_stats *stats);
/* Raw VGA6-bit RGB triplets, exactly768 bytes, no palette conversion here. */
dok_renderer_status dok_renderer_palette(uint8_t out[768],const dok_rom_assets *assets,unsigned level,unsigned language);
#endif
