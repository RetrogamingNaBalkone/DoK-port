/* Read-only observation ABI for N02 emulator checks, not a game/platform API. */
#ifndef DOK_NDS_DIAGNOSTIC_STATE_H
#define DOK_NDS_DIAGNOSTIC_STATE_H

#include <stdint.h>

#define NDS_DIAG_MAGIC UINT32_C(0x324b4f44) /* little endian "DOK2" */
#define NDS_DIAG_ABI_VERSION UINT32_C(1)
#define NDS_DIAG_BUTTON_COUNT 12

/* Every member is an ARM little-endian uint32_t. Button order:
 * A, B, X, Y, L, R, Up, Down, Left, Right, Start, Select.
 * Readers accept only equal even sequence values before/after a snapshot.
 * Storage results: 0 unchecked, 1 mount failed, 2 missing marker, 3 open failed,
 * 4 read failed, 5 different contents, 6 exact verified marker.
 * CPU/work ticks use BUS_CLOCK (33513982 Hz); system ticks use BUS_CLOCK / 64.
 */
typedef struct {
    uint32_t magic;
    uint32_t abi_version;
    uint32_t size_bytes;
    uint32_t sequence;
    uint32_t vblanks;
    uint32_t loops;
    uint32_t system_ticks_low;
    uint32_t system_ticks_high;
    uint32_t uptime_ms_low;
    uint32_t uptime_ms_high;
    uint32_t frame_us;
    uint32_t frame_min_us;
    uint32_t frame_max_us;
    uint32_t work_ticks;
    uint32_t work_us;
    uint32_t work_max_us;
    uint32_t missed_vblanks;
    uint32_t keys_held;
    uint32_t keys_down;
    uint32_t keys_up;
    uint32_t keys_seen_down;
    uint32_t keys_seen_up;
    uint32_t button_down_count[NDS_DIAG_BUTTON_COUNT];
    uint32_t button_up_count[NDS_DIAG_BUTTON_COUNT];
    uint32_t touch_held;
    uint32_t touch_x;
    uint32_t touch_y;
    uint32_t touch_raw_x;
    uint32_t touch_raw_y;
    uint32_t touch_down_count;
    uint32_t touch_up_count;
    uint32_t storage_result;
    uint32_t storage_errno;
    uint32_t storage_mounted;
    uint32_t storage_bytes_read;
    uint32_t storage_read_fnv1a;
    uint32_t storage_read_us;
    uint32_t storage_attempts;
    uint32_t audio_trigger_count;
    uint32_t audio_active;
} NdsDiagState;

extern volatile NdsDiagState g_nds_diag;

#endif
