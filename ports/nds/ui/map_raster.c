#include "dok/hotcode.h"
/* PKR 1098:1290 / PKE 1030:1374, Ghidra NE-loader addresses.
 * Independent NDS reconstruction; see tools/map/original_map.py execution oracle.
 * VGA primitives use inclusive ends. The original 3-pixel arrow remains fixed
 * at every map scale, as in the native function's immediate pixel offsets.
 */
#include "dok/map_raster.h"
#include <stddef.h>
#include <string.h>
#ifndef N23_MAP_VERTICAL
#define N23_MAP_VERTICAL 1
#endif
#ifndef N23_MAP_FLOOR_RUNS
#define N23_MAP_FLOOR_RUNS 0
#endif
#ifndef N23_MAP_FINE
#define N23_MAP_FINE 0
#endif
#ifndef N23_MAP_DIRECT_SEEN
#define N23_MAP_DIRECT_SEEN 0
#endif
#if N23_MAP_FINE
extern uint32_t cpuGetTiming(void);
uint32_t dok_n23_map_phases[4];
#endif

DOK_NDS_HOT_CODE static void pixel(uint8_t *out, int32_t x, int32_t y, uint8_t colour,
                                 dok_map_arrow_patch *patch)
{
    if ((uint32_t)x < 256u && (uint32_t)y < 160u) {
        unsigned offset=(unsigned)y*256u+(unsigned)x;
        if(patch){unsigned n=patch->count++;patch->offsets[n]=(uint16_t)offset;patch->colours[n]=out[offset];}
        out[offset] = colour;
    }
}

DOK_NDS_HOT_CODE static void clipped_rect(uint8_t *out, int32_t x1, int32_t y1,
                 int32_t x2, int32_t y2, uint8_t colour,
                 int32_t clip_x1,int32_t clip_y1,int32_t clip_x2,int32_t clip_y2)
{
    int32_t y, t;
    if (x1 > x2) { t = x1; x1 = x2; x2 = t; }
    if (y1 > y2) { t = y1; y1 = y2; y2 = t; }
    if (x2 < clip_x1 || y2 < clip_y1 || x1 > clip_x2 || y1 > clip_y2) return;
    if (x1 < clip_x1) x1 = clip_x1;
    if (y1 < clip_y1) y1 = clip_y1;
    if (x2 > clip_x2) x2 = clip_x2;
    if (y2 > clip_y2) y2 = clip_y2;
#if N23_MAP_VERTICAL
    if (x1 == x2) {
        uint8_t *column=out+(unsigned)y1*256u+(unsigned)x1;
        for (y=y1;y<=y2;++y,column+=256u)*column=colour;
        return;
    }
#endif
    for (y = y1; y <= y2; ++y)
        memset(out + (unsigned)y * 256u + (unsigned)x1, colour,
               (size_t)(x2 - x1 + 1));
}

DOK_NDS_HOT_CODE static unsigned native_class(const dok_map_snapshot *s, int index)
{
    if (index < 0 || index >= (int)s->width * s->length) return 0;
    return s->glyphs[s->maps[1][index]].class_id;
}

#if !N23_MAP_DIRECT_SEEN
DOK_NDS_HOT_CODE static int seen(const dok_map_snapshot *s, int index, int current)
{
    if (index < 0 || index >= (int)s->width * s->length) return 0;
    /* Native sets visited when encountering class 1 in its second pass.
     * Preserve this ordering without mutating the coherent world snapshot. */
    return s->visited[index] || (index <= current &&
        s->glyphs[s->maps[0][index]].class_id == 1);
}
#endif

DOK_NDS_HOT_CODE static int blocks_line(unsigned class_id)
{
    return class_id == 2 || class_id == 4 || class_id == 7;
}

#define rect(...) clipped_rect(__VA_ARGS__,clip_x1,clip_y1,clip_x2,clip_y2)
DOK_NDS_HOT_CODE static int raster_region(uint8_t *out, const dok_map_snapshot *s, unsigned scale,
                   int32_t offset_x, int32_t offset_z, dok_map_arrow_patch *patch,
                   int32_t clip_x1,int32_t clip_y1,int32_t clip_x2,int32_t clip_y2,int arrow)
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
#if N23_MAP_FINE
    uint32_t phase_stamp=cpuGetTiming(),phase_next;
#endif
    if(clip_x1<0)clip_x1=0;
    if(clip_y1<0)clip_y1=0;
    if(clip_x2>255)clip_x2=255;
    if(clip_y2>159)clip_y2=159;
    if(clip_x1>clip_x2||clip_y1>clip_y2)return 1;
    if(clip_x1==0&&clip_x2==255&&clip_y1==0&&clip_y2==159)
        memset(out, background, DOK_MAP_VIEW_BYTES);
    else for(int32_t row=clip_y1;row<=clip_y2;++row)
        memset(out+(unsigned)row*256u+(unsigned)clip_x1,background,(size_t)(clip_x2-clip_x1+1));
    count = (int)s->width * s->length;
    /* Visible outer rectangles form one contiguous range, including their
     * boundary strokes. Determine it once without per-cell clipping work. */
    xbegin = xend = s->width;
    for (x = 0; x < s->width; ++x) {
        right = offset_x + (int32_t)(s->width - x) * (int32_t)scale;
        left = right - (int32_t)scale;
        if (right >= clip_x1 && left <= clip_x2) {
            if (xbegin == s->width) xbegin = x;
            xend = x + 1;
        }
    }
    /* Native first pass fills FCMap, before any class-1 visited writes. */
#if N23_MAP_FINE
    phase_next=cpuGetTiming();dok_n23_map_phases[0]=phase_next-phase_stamp;phase_stamp=phase_next;
#endif
    for (z = 0; z < s->length; ++z) {
        top = offset_z + (int32_t)z * (int32_t)scale;
        if (top > clip_y2 || top + (int32_t)scale < clip_y1) continue;
        for (x = xbegin, index = (int)z * s->width + (int)xbegin;
             x < xend; ++x, ++index) {
            uint8_t colour = s->glyphs[s->maps[2][index]].marker;
            if (!s->visited[index] || colour == background || colour == excluded) continue;
            right = offset_x + (int32_t)(s->width - x) * (int32_t)scale;
#if N23_MAP_FLOOR_RUNS
            /* Same-colour consecutive inclusive rectangles have exactly
             * this rectangular union. Skip no unvisited/excluded cell;
             * different colours retain the native overwrite order. */
            unsigned run = 1;
            while (x + run < xend && s->visited[index + (int)run] &&
                   s->glyphs[s->maps[2][index + (int)run]].marker == colour) ++run;
            rect(out, right - (int32_t)(run * scale), top, right, top + (int32_t)scale, colour);
            x += run - 1u;
            index += (int)run - 1;
#else
            rect(out, right - (int32_t)scale, top, right, top + (int32_t)scale, colour);
#endif
        }
    }
#if N23_MAP_FINE
    phase_next=cpuGetTiming();dok_n23_map_phases[1]=phase_next-phase_stamp;phase_stamp=phase_next;
#endif
    for (z = 0; z < s->length; ++z) {
        top = offset_z + (int32_t)z * (int32_t)scale;
        if (top > clip_y2 || top + (int32_t)scale < clip_y1) continue;
        for (x = xbegin, index = (int)z * s->width + (int)xbegin;
             x < xend; ++x, ++index) {
            const dok_map_glyph *o = &s->glyphs[s->maps[0][index]];
            unsigned class_id = o->class_id;
            uint8_t colour;
#if N23_MAP_DIRECT_SEEN
            if (!s->visited[index] && class_id != 1u) continue;
#else
            if (!seen(s, index, index)) continue;
#endif
            if (!class_id) o = &s->glyphs[s->maps[1][index]];
            class_id = o->class_id;
            colour = o->marker;
            right = offset_x + (int32_t)(s->width - x) * (int32_t)scale;
            left = right - (int32_t)scale;
            if (class_id == 2) {
#if N23_MAP_DIRECT_SEEN
                /* Loop index is valid. Native earlier-neighbour class1
                 * visibility applies only to left/up, never right/down.
                 * Keep native cross-row +/-1 behavior and primitive order. */
                if (index > 0 && (s->visited[index-1] || s->glyphs[s->maps[0][index-1]].class_id==1u) &&
                    s->glyphs[s->maps[1][index-1]].class_id!=2u)
                    rect(out,right,top,right,top+(int32_t)scale,colour);
                if (z && (s->visited[index-s->width] || s->glyphs[s->maps[0][index-s->width]].class_id==1u) &&
                    s->glyphs[s->maps[1][index-s->width]].class_id!=2u)
                    rect(out,left,top,right,top,colour);
                if (index+1<count && s->visited[index+1] && s->glyphs[s->maps[1][index+1]].class_id!=2u)
                    rect(out,left,top,left,top+(int32_t)scale,colour);
                if (index+s->width<count && s->visited[index+s->width] &&
                    s->glyphs[s->maps[1][index+s->width]].class_id!=2u)
                    rect(out,left,top+(int32_t)scale,right,top+(int32_t)scale,colour);
#else
                if (seen(s, index - 1, index) && native_class(s, index - 1) != 2)
                    rect(out, right, top, right, top + (int32_t)scale, colour);
                if (z && seen(s, index - s->width, index) && native_class(s, index - s->width) != 2)
                    rect(out, left, top, right, top, colour);
                if (seen(s, index + 1, index) && native_class(s, index + 1) != 2)
                    rect(out, left, top, left, top + (int32_t)scale, colour);
                if (index + s->width < count && seen(s, index + s->width, index) &&
                    native_class(s, index + s->width) != 2)
                    rect(out, left, top + (int32_t)scale, right, top + (int32_t)scale, colour);
#endif
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
#if N23_MAP_FINE
    phase_next=cpuGetTiming();dok_n23_map_phases[2]=phase_next-phase_stamp;phase_stamp=phase_next;
#endif
    if(arrow)dok_map_arrow_draw(out,s,scale,offset_x,offset_z,patch);
#if N23_MAP_FINE
    dok_n23_map_phases[3]=cpuGetTiming()-phase_stamp;
#endif
    return 1;
}
#undef rect

int dok_map_raster(uint8_t *out,const dok_map_snapshot *s,unsigned scale,int32_t x,int32_t z)
{
    return dok_map_raster_patched(out,s,scale,x,z,NULL);
}

int dok_map_raster_patched(uint8_t *out,const dok_map_snapshot *s,unsigned scale,
                          int32_t x,int32_t z,dok_map_arrow_patch *patch)
{
    return raster_region(out,s,scale,x,z,patch,0,0,255,159,1);
}

int dok_map_raster_region(uint8_t *out,const dok_map_snapshot *s,unsigned scale,
                         int32_t x,int32_t z,int32_t x1,int32_t y1,int32_t x2,int32_t y2)
{
    return raster_region(out,s,scale,x,z,NULL,x1,y1,x2,y2,0);
}

void dok_map_arrow_restore(uint8_t *out,const dok_map_arrow_patch *patch)
{
    for(unsigned n=patch->count;n>0;--n)out[patch->offsets[n-1]]=patch->colours[n-1];
}

void dok_map_arrow_draw(uint8_t *out,const dok_map_snapshot *s,unsigned scale,
                        int32_t offset_x,int32_t offset_z,dok_map_arrow_patch *patch)
{
    int32_t left,top;
    if(patch)patch->count=0;
    /* Drawing once after the traversal removes native uninitialized-stack
     * residue; the three final pixels and native angle cutoffs are preserved. */
    if (s->hero_x >= 0 && s->hero_x < s->width && s->hero_z >= 0 && s->hero_z < s->length) {
        static const int8_t dx[8] = {2,3,3,3,2,1,1,1};
        static const int8_t dz[8] = {3,3,2,1,1,1,2,3};
        unsigned h = s->heading;
        unsigned sector = dok_map_arrow_sector(h);
        if (sector < 8) {
            unsigned hero_index = (unsigned)s->hero_z * s->width + (unsigned)s->hero_x;
            uint8_t colour = s->glyphs[s->maps[0][hero_index]].marker;
            left = offset_x + (int32_t)(s->width - s->hero_x - 1) * (int32_t)scale;
            top = offset_z + (int32_t)s->hero_z * (int32_t)scale;
            pixel(out, left + dx[sector], top + dz[sector], colour,patch);
            pixel(out, left + 2, top + 2, 0,patch);
            pixel(out, left + 4 - dx[sector], top + 4 - dz[sector], 0,patch);
        }
    }
}
