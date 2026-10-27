#ifndef DOK_GBA_HW_SCRATCH_H
#define DOK_GBA_HW_SCRATCH_H
/* Two completed Mode2 affine configurations:161*(two16-byte matrices+4-byte
 * BG controls)*2. Occupies the tail of the unused hardware interval payload. */
#define DOK_HW_LOW_FRAME_BYTES 11592u
#define DOK_HW_OAM_FRAME_BYTES 5152u
#ifdef DOK_RENDER_HW_OAM_RAM_PROTOTYPE
#define DOK_HW_OAM_RESERVED_BYTES DOK_HW_OAM_FRAME_BYTES
#else
#define DOK_HW_OAM_RESERVED_BYTES 0u
#endif
#endif
