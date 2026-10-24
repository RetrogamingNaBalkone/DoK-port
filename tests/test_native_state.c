#include "dok/native_state.h"
#include <stdio.h>
#include <string.h>
#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "line %d: %s\n", __LINE__, #expr); return 1; } } while (0)
int main(void) {
    uint8_t player[0xde] = {0};
    /* Values chosen to distinguish sign, endian, field boundaries and far halves. */
    const uint8_t event[16] = {0xff,0xff,0xff,0xff, 0,0,0,0x80, 0x34,0x12,0x78,0x56, 1,2,3,4};
    dok_native_event e;
    dok_player_state s, unchanged;
    player[0x12] = 1;
    player[0x26] = 0xff; player[0x27] = 0xff; player[0x28] = 0xff; player[0x29] = 0xff;
    player[0x2d] = 0x80; player[0x2e] = 0x34; player[0x2f] = 0x12;
    player[0x32] = 0xff; player[0x33] = 0xff;
    player[0x5e] = 0x00; player[0x5f] = 0x80;
    player[0x60] = 0xff; player[0x61] = 0x7f;
    player[0x62] = 100; player[0x64] = 90; player[0x73] = 0x80;
    player[0xca] = 0x34; player[0xcb] = 0x12; player[0xcc] = 0x78; player[0xcd] = 0x56;
    player[0xce] = 8; player[0xd0] = 2; player[0xd6] = 3; player[0xd8] = 99;
    CHECK(dok_native_read_player(player, sizeof(player), &s));
    CHECK(s.x == -1 && s.y == INT32_MIN && s.z == 0x1234 && s.heading == -1);
    CHECK(s.health == INT16_MIN && s.energy == INT16_MAX && s.health_max == 100 && s.energy_max == 90);
    CHECK(s.keys == 0x8000 && s.weapons[8].offset == 0x1234 && s.weapons[8].selector == 0x5678);
    CHECK(s.selected_weapon == 8 && s.pending_weapon == 2 && s.lives == 3 && s.score == 99);
    memcpy(&unchanged, &s, sizeof(s));
    CHECK(!dok_native_read_player(player, sizeof(player)-1, &s));
    CHECK(!memcmp(&s, &unchanged, sizeof(s)));
    player[0x12] = 9;
    CHECK(!dok_native_read_player(player, sizeof(player), &s));
    CHECK(!memcmp(&s, &unchanged, sizeof(s)));
    CHECK(dok_native_read_event(event, sizeof(event), &e));
    CHECK(e.deadline == UINT32_MAX && e.interval == UINT32_C(0x80000000));
    CHECK(e.callback.offset == 0x1234 && e.callback.selector == 0x5678 && e.parameter == UINT32_C(0x04030201));
    CHECK(!dok_native_read_event(event, 15, &e));
    puts("native adapters: signed values, bounds and opaque far-pointer halves passed");
    return 0;
}
