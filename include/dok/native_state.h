#ifndef DOK_NATIVE_STATE_H
#define DOK_NATIVE_STATE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Read-only adapters for verified PKR/PKE layouts, not packed native classes.
 * Far pointers are opaque tokens, never host addresses. See docs/native-state.md.
 */
typedef struct dok_far16 { uint16_t offset, selector; } dok_far16;
typedef struct dok_native_event {
    uint32_t deadline, interval;
    dok_far16 callback;
    uint32_t parameter;
} dok_native_event;
typedef struct dok_player_state {
    int32_t x, y, z;
    int16_t heading, health, energy, health_max, energy_max;
    uint16_t keys;
    int16_t selected_weapon, pending_weapon, lives, score;
    dok_far16 weapons[9];
} dok_player_state;

/* Returns 1 on success. On failure the output is untouched. No pointer traversal. */
int dok_native_read_event(const void *data, size_t size, dok_native_event *out);
int dok_native_read_player(const void *data, size_t size, dok_player_state *out);
#ifdef __cplusplus
}
#endif
#endif
