#include "dok/runtime.h"
#include <stddef.h>

/* Linked only by -MemoryProbe. These are measured ABI values, not hand estimates. */
const uint32_t dok_memory_layout[] __attribute__((used, section(".rodata.memory_probe"))) = {
    0x324d454d, 1,
    sizeof(void *),
    sizeof(dok_runtime_object), _Alignof(dok_runtime_object),
    sizeof(dok_runtime_actor), _Alignof(dok_runtime_actor),
    sizeof(dok_runtime_player), _Alignof(dok_runtime_player),
    sizeof(dok_runtime_definition), _Alignof(dok_runtime_definition),
    sizeof(dok_runtime_field), _Alignof(dok_runtime_field),
    sizeof(dok_runtime_level), _Alignof(dok_runtime_level),
    sizeof(dok_runtime_storage), _Alignof(dok_runtime_storage),
    sizeof(dok_runtime), _Alignof(dok_runtime),
    sizeof(dok_world_object), sizeof(dok_level_object),
    offsetof(dok_runtime, objects), offsetof(dok_runtime, actors),
    offsetof(dok_runtime, player), offsetof(dok_runtime, maps),
    offsetof(dok_runtime, object_count),
    DOK_RUNTIME_MAX_OBJECTS, DOK_RUNTIME_MAX_ACTORS,
    DOK_RUNTIME_MAX_FIELDS, DOK_RUNTIME_MAX_TEXT, DOK_RUNTIME_MAX_CELLS
};

dok_runtime g_runtime_memory_probe __attribute__((used, section(".bss.runtime_probe")));

/* G02 engineering envelopes for future stages, not implemented subsystems.
 * Allocating them in a separate probe ELF makes the total linker-checkable. */
#define RESERVE(name, count) uint8_t name[count] __attribute__((used, aligned(4), section(".bss.future_reserves")))
RESERVE(g_frame_reserve, 38400);
RESERVE(g_renderer_reserve, 4096);
RESERVE(g_texture_reserve, 65536);
RESERVE(g_audio_reserve, 8192);
RESERVE(g_event_reserve, 16384);
RESERVE(g_save_reserve, 4096);
RESERVE(g_scratch_reserve, 8192);

/* Small immutable fixture executes the real runtime reset on ARM. The six
 * original worlds are checked independently on the host; this is an ABI probe. */
static const dok_runtime_definition probe_definition = {
    .class_id = 1, .glyph = 'M', .actor_index = 0, .placements = 1
};
static const dok_runtime_field probe_field = {0};
static const dok_runtime_object probe_object = {0};
static const dok_runtime_actor probe_actor = {
    .known = DOK_RUNTIME_X | DOK_RUNTIME_Y | DOK_RUNTIME_Z,
    .x = 240, .y = -300, .z = 240, .heading = 90
};
static const dok_runtime_player probe_player = {.health = 100, .lives = 3};
static const uint8_t probe_text[1] = {0}, probe_maps[3] = {'M', 'M', 'M'};
static const dok_runtime_level probe_level = {
    .settings = {1, 1, 1, 480, 960, 10, 143, 238},
    .object_count = 1, .actor_count = 1, .cells = 1, .player_index = 1,
    .glyph_index = {['M'] = 1}, .definitions = &probe_definition,
    .fields = &probe_field, .text = probe_text, .maps = probe_maps,
    .initial_objects = &probe_object, .initial_actors = &probe_actor,
    .initial_player = &probe_player
};
unsigned gba_runtime_selftest(void)
{
    int32_t x = 0, y = 0, z = 0;
    dok_runtime *r = &g_runtime_memory_probe;
    if (dok_runtime_reset(r, &probe_level) != DOK_RUNTIME_OK) return 1;
    if (!dok_runtime_position(r, 0, &x, &y, &z) || x != 240 || y != -300 || z != 240) return 2;
    r->actors[0].x = 999; r->player.health = 1; r->maps[0] = 0; r->maps[3] = 1;
    if (dok_runtime_reset(r, &probe_level) != DOK_RUNTIME_OK || r->actors[0].x != 240 ||
        r->player.health != 100 || r->maps[0] != 'M' || r->maps[3] != 0) return 3;
    dok_runtime_release(r);
    if (r->level || r->object_count || dok_runtime_position(r, 0, &x, &y, &z)) return 4;
    return 0;
}
