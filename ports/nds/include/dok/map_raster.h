#ifndef DOK_MAP_RASTER_H
#define DOK_MAP_RASTER_H

#include <stdint.h>

#define DOK_MAP_VIEW_WIDTH 256
#define DOK_MAP_VIEW_HEIGHT 160
#define DOK_MAP_VIEW_BYTES (DOK_MAP_VIEW_WIDTH * DOK_MAP_VIEW_HEIGHT)

typedef struct dok_map_glyph {
    uint16_t class_id;
    uint8_t marker;
    uint8_t monster_state;
} dok_map_glyph;

typedef struct dok_map_snapshot {
    const uint8_t *maps[3]; /* MMap, TMap, FCMap, width*length bytes each. */
    const uint8_t *visited;
    const dok_map_glyph *glyphs; /* 256 entries, immutable for this draw. */
    uint16_t width, length;
    int16_t hero_x, hero_z;
    uint16_t heading;
    uint8_t map_available;
} dok_map_snapshot;

/* Screen coordinates of cell x's right boundary and row z's top boundary:
 * offset_x + (width-x)*scale, offset_z + z*scale.
 * offset_x/offset_z are minus the viewport left/top in the map plane.
 * Only the first 256*160 bytes are written; the button strip is separate.
 * Returns zero for malformed inputs/unsupported scale, one on success. */
int dok_map_raster(uint8_t *out, const dok_map_snapshot *snapshot,
                   unsigned scale, int32_t offset_x, int32_t offset_z);

#endif
