#include "dok/rom_assets.h"
#include <stdint.h>

extern const uint8_t dok_gba_assets[], dok_gba_assets_end[];
struct assets_diagnostics {
    uint32_t magic, status, language, level, views_checked, resources, package_bytes;
    uint32_t text_hash, state_hash, switches, text_size, last_keys;
    int32_t player_x, player_y, player_z;
    uint32_t startup_switch_preserved;
};
volatile struct assets_diagnostics g_assets_diag;
dok_runtime g_assets_world;
dok_runtime_level g_assets_view;
static dok_rom_assets package;

static uint32_t hash_bytes(const void *data, size_t size)
{
    const uint8_t *bytes = data;
    uint32_t h = 2166136261u;
    while (size--) h = (h ^ *bytes++) * 16777619u;
    return h;
}

static void snapshot(void)
{
    int32_t x = 0, y = 0, z = 0;
    g_assets_diag.text_hash = hash_bytes(g_assets_view.text, g_assets_view.text_size);
    g_assets_diag.text_size = g_assets_view.text_size;
    g_assets_diag.state_hash = hash_bytes(&g_assets_world, sizeof(g_assets_world));
    if (g_assets_world.player_index && dok_runtime_position(&g_assets_world,
        (uint16_t)(g_assets_world.player_index - 1), &x, &y, &z)) {
        g_assets_diag.player_x = x;
        g_assets_diag.player_y = y;
        g_assets_diag.player_z = z;
    }
}

unsigned gba_assets_selftest(void)
{
    dok_rom_status status;
    size_t size = (size_t)((uintptr_t)dok_gba_assets_end - (uintptr_t)dok_gba_assets);
    g_assets_diag.magic = 0x41535433;
    status = dok_rom_open(&package, dok_gba_assets, size);
    if (status != DOK_ROM_OK) {g_assets_diag.status = status; return 1;}
    g_assets_diag.resources = package.resource_count;
    g_assets_diag.package_bytes = (uint32_t)size;
    for (unsigned level = 0; level < 3; ++level) {
        for (unsigned language = 0; language < 2; ++language) {
            status = dok_rom_level(&g_assets_view, &package, level, language);
            if (status != DOK_ROM_OK) {g_assets_diag.status = status; return 2;}
            if (dok_runtime_reset(&g_assets_world, &g_assets_view) != DOK_RUNTIME_OK) return 3;
            g_assets_diag.views_checked++;
        }
    }
    if (dok_rom_level(&g_assets_view, &package, 0, 0) != DOK_ROM_OK ||
        dok_runtime_reset(&g_assets_world, &g_assets_view) != DOK_RUNTIME_OK) return 4;
    /* Mutate actual gameplay values before switching; no reset is permitted. */
    g_assets_world.player.health = 77;
    g_assets_world.maps[3u*g_assets_view.cells] = 1;
    uint32_t state = hash_bytes(&g_assets_world, sizeof(g_assets_world));
    if (dok_rom_switch_language(&g_assets_world, &g_assets_view, &package, 1) != DOK_ROM_OK ||
        state != hash_bytes(&g_assets_world, sizeof(g_assets_world))) return 5;
    if (dok_rom_switch_language(&g_assets_world, &g_assets_view, &package, 0) != DOK_ROM_OK ||
        state != hash_bytes(&g_assets_world, sizeof(g_assets_world))) return 6;
    g_assets_diag.startup_switch_preserved = 1;
    if (dok_runtime_reset(&g_assets_world, &g_assets_view) != DOK_RUNTIME_OK) return 7;
    snapshot();
    return 0;
}

void gba_assets_input(uint32_t keys)
{
    if ((keys & 4u) && !(g_assets_diag.last_keys & 4u)) {
        unsigned language = g_assets_diag.language ^ 1u;
        dok_rom_status status = dok_rom_switch_language(&g_assets_world, &g_assets_view, &package, language);
        g_assets_diag.status = status;
        if (status == DOK_ROM_OK) {
            g_assets_diag.language = language;
            g_assets_diag.switches++;
            snapshot();
        }
    }
    g_assets_diag.last_keys = keys;
}
const char *gba_assets_language(void) {return g_assets_diag.language ? "EN" : "RU";}
