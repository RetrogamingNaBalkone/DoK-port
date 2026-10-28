#include "dok/native_state.h"

static uint16_t u16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static int16_t s16(const uint8_t *p) {
    uint16_t n = u16(p);
    return n <= INT16_MAX ? (int16_t)n : (int16_t)(-1 - (int16_t)(UINT16_MAX - n));
}
static int32_t s32(const uint8_t *p) {
    uint32_t n = u32(p);
    return n <= INT32_MAX ? (int32_t)n : -1 - (int32_t)(UINT32_MAX - n);
}
static dok_far16 far16(const uint8_t *p) {
    dok_far16 result;
    result.offset = u16(p); result.selector = u16(p + 2);
    return result;
}
int dok_native_read_event(const void *data, size_t size, dok_native_event *out) {
    const uint8_t *p = (const uint8_t *)data;
    dok_native_event event;
    if (!data || !out || size < 16u) return 0;
    event.deadline = u32(p); event.interval = u32(p + 4);
    event.callback = far16(p + 8); event.parameter = u32(p + 12);
    *out = event;
    return 1;
}
int dok_native_read_player(const void *data, size_t size, dok_player_state *out) {
    const uint8_t *p = (const uint8_t *)data;
    dok_player_state state;
    size_t i;
    if (!data || !out || size < 0xdeu) return 0;
    if (u16(p + 0x12) != 1u) return 0;
    state.x = s32(p + 0x26); state.y = s32(p + 0x2a); state.z = s32(p + 0x2e);
    state.heading = s16(p + 0x32);
    state.health = s16(p + 0x5e); state.energy = s16(p + 0x60);
    state.health_max = s16(p + 0x62); state.energy_max = s16(p + 0x64);
    state.keys = u16(p + 0x72);
    for (i = 0; i < 9; ++i) state.weapons[i] = far16(p + 0xaa + i * 4u);
    state.selected_weapon = s16(p + 0xce); state.pending_weapon = s16(p + 0xd0);
    state.lives = s16(p + 0xd6); state.score = s16(p + 0xd8);
    *out = state;
    return 1;
}
