#include "dok/hotcode.h"
/* PKR 1098:1290 / PKE 1030:1374, Ghidra NE-loader addresses.
 * Independent NDS reconstruction; see tools/map/original_map.py execution oracle.
 * VGA primitives use inclusive ends. The original 3-pixel arrow remains fixed
 * at every map scale, as in the native function's immediate pixel offsets.
 */
#include "dok/map_raster.h"
#include <stddef.h>
#include <string.h>

DOK_NDS_HOT_CODE static void pixel(uint8_t *out, int32_t x, int32_t y, uint8_t colour)
{
    if ((uint32_t)x < 256u && (uint32_t)y < 160u)
        out[(unsigned)y * 256u + (unsigned)x] = colour;
}

DOK_NDS_HOT_CODE static void rect(uint8_t *out, int32_t x1, int32_t y1,
                 int32_t x2, int32_t y2, uint8_t colour)
{
    int32_t y, t;
    if (x1 > x2) { t = x1; x1 = x2; x2 = t; }
    if (y1 > y2) { t = y1; y1 = y2; y2 = t; }
    if (x2 < 0 || y2 < 0 || x1 > 255 || y1 > 159) return;
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 > 255) x2 = 255;
    if (y2 > 159) y2 = 159;
    for (y = y1; y <= y2; ++y)
        memset(out + (unsigned)y * 256u + (unsigned)x1, colour,
               (size_t)(x2 - x1 + 1));
}

DOK_NDS_HOT_CODE static unsigned native_class(const dok_map_snapshot *s, int index)
{
    if (index < 0 || index >= (int)s->width * s->length) return 0;
    return s->glyphs[s->maps[1][index]].class_id;
}

DOK_NDS_HOT_CODE static int seen(const dok_map_snapshot *s, int index, int current)
{
    if (index < 0 || index >= (int)s->width * s->length) return 0;
    /* Native sets visited when encountering class 1 in its second pass.
     * Preserve this ordering without mutating the coherent world snapshot. */
    return s->visited[index] || (index <= current &&
        s->glyphs[s->maps[0][index]].class_id == 1);
}

DOK_NDS_HOT_CODE static int blocks_line(unsigned class_id)
{
    return class_id == 2 || class_id == 4 || class_id == 7;
}

DOK_NDS_HOT_CODE int dok_map_raster(uint8_t *out, const dok_map_snapshot *s, unsigned scale,
                   int32_t offset_x, int32_t offset_z)
{
    unsigned x, z, xbegin, xend;
    uint8_t background, excluded;
    int index, count;
    int32_t right, top, left;
    if (!out || !s || !s->maps[0] || !s->maps[1] || !s->maps[2] ||
        !s->visited || !s->glyphs || !s->width || !s->length ||
        s->width > 64 || s->length > 64 ||
        (scale != 2 && scale != 3 && scale != 4 && scale != 6 && scale != 8) ||
        offset_x < -32768 || offset_x > 32767 ||
        offset_z < -32768 || offset_z > 32767) return 0;
    background = s->glyphs[32].marker;
    excluded = s->glyphs[219].marker;
    memset(out, background, DOK_MAP_VIEW_BYTES);
    count = (int)s->width * s->length;
    /* Visible outer rectangles form one contiguous range, including their
     * boundary strokes. Determine it once without per-cell clipping work. */
    xbegin = xend = s->width;
    for (x = 0; x < s->width; ++x) {
        right = offset_x + (int32_t)(s->width - x) * (int32_t)scale;
        left = right - (int32_t)scale;
        if (right >= 0 && left <= 255) {
            if (xbegin == s->width) xbegin = x;
            xend = x + 1;
        }
    }
    /* Native first pass fills FCMap, before any class-1 visited writes. */
    for (z = 0; z < s->length; ++z) {
        top = offset_z + (int32_t)z * (int32_t)scale;
        if (top > 159 || top + (int32_t)scale < 0) continue;
        for (x = xbegin, index = (int)z * s->width + (int)xbegin;
             x < xend; ++x, ++index) {
            uint8_t colour = s->glyphs[s->maps[2][index]].marker;
            if (!s->visited[index] || colour == background || colour == excluded) continue;
            right = offset_x + (int32_t)(s->width - x) * (int32_t)scale;
            rect(out, right - (int32_t)scale, top, right, top + (int32_t)scale, colour);
        }
    }
    for (z = 0; z < s->length; ++z) {
        top = offset_z + (int32_t)z * (int32_t)scale;
        if (top > 159 || top + (int32_t)scale < 0) continue;
        for (x = xbegin, index = (int)z * s->width + (int)xbegin;
             x < xend; ++x, ++index) {
            const dok_map_glyph *o = &s->glyphs[s->maps[0][index]];
            unsigned class_id = o->class_id;
            uint8_t colour;
            if (!seen(s, index, index)) continue;
            if (!class_id) o = &s->glyphs[s->maps[1][index]];
            class_id = o->class_id;
            colour = o->marker;
            right = offset_x + (int32_t)(s->width - x) * (int32_t)scale;
            left = right - (int32_t)scale;
            if (class_id == 2) {
                if (seen(s, index - 1, index) && native_class(s, index - 1) != 2)
                    rect(out, right, top, right, top + (int32_t)scale, colour);
                if (z && seen(s, index - s->width, index) && native_class(s, index - s->width) != 2)
                    rect(out, left, top, right, top, colour);
                if (seen(s, index + 1, index) && native_class(s, index + 1) != 2)
                    rect(out, left, top, left, top + (int32_t)scale, colour);
                if (index + s->width < count && seen(s, index + s->width, index) &&
                    native_class(s, index + s->width) != 2)
                    rect(out, left, top + (int32_t)scale, right, top + (int32_t)scale, colour);
            } else {
                if ((s->map_available && (class_id == 3 || class_id == 10 ||
                     class_id == 6 || class_id == 13 ||
                     (class_id == 9 && o->monster_state != 4))) ||
                    class_id == 8 || class_id == 12)
                    rect(out, left + 1, top + 1, right - 1, top + (int32_t)scale - 1, colour);
                if (class_id == 4 || class_id == 7) {
                    if (!blocks_line(native_class(s, index - 1)) &&
                        !blocks_line(native_class(s, index + 1)))
                        rect(out, right - (int32_t)(scale >> 1), top + 1,
                             right - (int32_t)(scale >> 1), top + (int32_t)scale, colour);
                    if (!blocks_line(native_class(s, index - s->width)) &&
                        !blocks_line(native_class(s, index + s->width)))
                        rect(out, left, top + (int32_t)(scale >> 1),
                             right - 1, top + (int32_t)(scale >> 1), colour);
                }
            }
        }
    }
    /* Drawing once after the traversal removes native uninitialized-stack
     * residue; the three final pixels and native angle cutoffs are preserved. */
    if (s->hero_x >= 0 && s->hero_x < s->width && s->hero_z >= 0 && s->hero_z < s->length) {
        static const int8_t dx[8] = {2,3,3,3,2,1,1,1};
        static const int8_t dz[8] = {3,3,2,1,1,1,2,3};
        unsigned h = s->heading;
        unsigned sector = (h <= 19 || (h >= 341 && h <= 360)) ? 0 :
            h <= 64 ? 1 : h <= 109 ? 2 : h <= 154 ? 3 : h <= 199 ? 4 :
            h <= 244 ? 5 : h <= 289 ? 6 : h <= 340 ? 7 : 8;
        if (sector < 8) {
            unsigned hero_index = (unsigned)s->hero_z * s->width + (unsigned)s->hero_x;
            uint8_t colour = s->glyphs[s->maps[0][hero_index]].marker;
            left = offset_x + (int32_t)(s->width - s->hero_x - 1) * (int32_t)scale;
            top = offset_z + (int32_t)s->hero_z * (int32_t)scale;
            pixel(out, left + dx[sector], top + dz[sector], colour);
            pixel(out, left + 2, top + 2, 0);
            pixel(out, left + 4 - dx[sector], top + 4 - dz[sector], 0);
        }
    }
    return 1;
}
