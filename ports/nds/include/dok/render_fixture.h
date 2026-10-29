#ifndef DOK_RENDER_FIXTURE_H
#define DOK_RENDER_FIXTURE_H
#include <stdint.h>

/* Diagnostic input only, never a gameplay/save or replacement asset format.
 * Read each LE word explicitly; never overlay this native struct on disk.
 * 64B header: magic[8]="N07SCN1\0", version=1,count,recordbytes=64,
 * levelbytes=10240,totalbytes,reserved=0, SHA256(body)[32].
 * Body: count records, then 3 blocks of palette[256] LE RGB555 + HUD[9728].
 * Palette index0 is transparent in the 2D HUD, top154rows are implicit zero.
 * Every nontransparent HUD pixel is remapped to index1..255 offline.
 */
#define DOK_RENDER_FIXTURE_HEADER 64u
#define DOK_RENDER_FIXTURE_RECORD 64u
#define DOK_RENDER_FIXTURE_LEVEL 10240u
#define DOK_RENDER_FIXTURE_CASES 64u
#define DOK_RENDER_CASE_FLAT 1u
#define DOK_RENDER_CASE_MOVING 2u
#define DOK_RENDER_CASE_WARMUP 4u

typedef struct dok_render_case {
    uint32_t id,level;
    int32_t x,y,z;
    uint32_t heading;
    int32_t dx,dz,dheading;
    uint32_t frames,flags,source_kind,cell_x,cell_z,reserved0,reserved1;
} dok_render_case;
_Static_assert(sizeof(dok_render_case)==DOK_RENDER_FIXTURE_RECORD,"native case word count");
#endif
