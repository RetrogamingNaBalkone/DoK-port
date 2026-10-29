/* N10 host-only transport for independent original instruction comparison.
 * Little-endian wire records are decoded explicitly; no C structure ABI is
 * used on stdin/stdout. This client computes no geometry of its own. */
#include "dok/collision.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "dok/input.h"
#include "render_trig.inc"
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

static unsigned char maps[3][4096], known[256];
static uint16_t flags[256], classes[256];
static dok_level_definition definition;
static dok_initial_world world;
static dok_collision_state collision;
static unsigned char hero_glyph;

static int trig(void *context, uint32_t heading, int32_t *sine, int32_t *cosine)
{
    (void)context;
    if (heading > 360u) return 0;
    *sine = dok_render_sine[heading]; *cosine = dok_render_cosine[heading]; return 1;
}

static int initialize_world(const dok_collision_view *view)
{
    uint32_t glyph, index = 0u;
    size_t cells = (size_t)view->width * view->length;
    memset(&definition, 0, sizeof definition);
    memset(&world, 0, sizeof world);
    memset(&collision, 0, sizeof collision);
    definition.language = 1u;
    definition.settings[0] = (int16_t)view->width; definition.settings[1] = 1;
    definition.settings[2] = (int16_t)view->length; definition.settings[3] = view->cell_side;
    definition.settings[7] = view->min_distance; world.epoch = 1u;
    world.definition = &definition;
    for (glyph = 0u; glyph < 256u; ++glyph) if (known[glyph] != 0u) {
        dok_level_object *d = &definition.objects[index];
        dok_world_object_seed *o = &world.objects[index];
        d->glyph = (unsigned char)glyph; d->class_id = classes[glyph]; d->flags = flags[glyph];
        definition.glyph_index[glyph] = (uint16_t)(index + 1u);
        world.glyph_index[glyph] = (uint16_t)(index + 1u);
        o->reference = (dok_object_ref){1u, index + 1u, 1u};
        o->definition_index = (uint16_t)index; o->class_id = classes[glyph];
        if (glyph == hero_glyph) { world.hero = o->reference; o->pose_known = 1u; }
        ++index;
    }
    definition.object_count = (uint16_t)index; world.object_count = (uint16_t)index;
    memcpy(world.maps[0], maps[0], cells); memcpy(world.maps[1], maps[1], cells); memcpy(world.maps[2], maps[2], cells);
    return dok_collision_bind(&collision, &world) == DOK_STATUS_OK;
}

static int bytes(void *destination, size_t size)
{
    return fread(destination, 1u, size, stdin) == size;
}
static uint32_t u32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static int32_t i32(const unsigned char *p)
{
    uint32_t bits = u32(p);
    return bits <= INT32_MAX ? (int32_t)bits :
        (int32_t)((int64_t)bits - INT64_C(4294967296));
}
static int emit(int32_t value)
{
    uint32_t bits = (uint32_t)value;
    unsigned char raw[4] = {(unsigned char)bits, (unsigned char)(bits >> 8),
        (unsigned char)(bits >> 16), (unsigned char)(bits >> 24)};
    return fwrite(raw, 1u, sizeof raw, stdout) == sizeof raw;
}
int main(void)
{
    unsigned char raw[1024];
    dok_collision_view view;
    int32_t width, length, side, radius;
    size_t count, j;
#ifdef _WIN32
    if (_setmode(_fileno(stdin), _O_BINARY) == -1 ||
        _setmode(_fileno(stdout), _O_BINARY) == -1) return 2;
#endif
    if (!bytes(raw, 20u)) return 2;
    width = i32(raw); length = i32(raw + 4); side = i32(raw + 8); radius = i32(raw + 12);
    hero_glyph = (unsigned char)i32(raw + 16);
    if (width < 1 || width > 64 || length < 1 || length > 64 ||
        side < 1 || side > INT16_MAX || radius < INT16_MIN || radius > INT16_MAX) return 2;
    view.width = (uint16_t)width; view.length = (uint16_t)length;
    view.cell_side = (int16_t)side; view.min_distance = (int16_t)radius;
    if (!bytes(raw, 512u)) return 2;
    for (j = 0u; j < 256u; ++j) flags[j] = (uint16_t)((uint16_t)raw[j * 2u] | ((uint16_t)raw[j * 2u + 1u] << 8));
    if (!bytes(raw, 512u)) return 2;
    for (j = 0u; j < 256u; ++j) classes[j] = (uint16_t)((uint16_t)raw[j * 2u] | ((uint16_t)raw[j * 2u + 1u] << 8));
    if (!bytes(known, sizeof known)) return 2;
    count = (size_t)view.width * view.length;
    for (j = 0u; j < 3u; ++j) if (!bytes(maps[j], count)) return 2;
    view.mmap = maps[0]; view.tmap = maps[1]; view.known = known; view.flags = flags;
    view.projectile_occupied = NULL;
    for (;;) {
        int32_t op, x, z, nx, nz;
        uint32_t clear = 0u;
        size_t got = fread(raw, 1u, 20u, stdin);
        if (got == 0u && feof(stdin)) return fflush(stdout) == 0 ? 0 : 2;
        if (got != 20u) return 2;
        op = i32(raw); x = i32(raw + 4); z = i32(raw + 8); nx = i32(raw + 12); nz = i32(raw + 16);
        if (op == 0) {
            if (dok_collision_native_clear(&view, x, z, view.min_distance, &clear) != DOK_STATUS_OK) return 3;
            nx = x; nz = z;
        } else if (op == 1) {
            if (dok_collision_native_resolve(&view, x, z, nx, nz, &nx, &nz) != DOK_STATUS_OK) return 3;
        } else if (op == 2 || op == 3) {
            dok_motion_state motion;
            dok_motion_report movement;
            dok_collision_context context;
            dok_collision_report report = {0};
            dok_motion_services services = {NULL, trig, &context, dok_collision_move};
            dok_status status;
            int32_t distance = nx;
            uint32_t k;
            if (!initialize_world(&view)) return 4;
            if (distance < -32767 || distance > 32767 || nz < 0 || nz > 360) return 2;
            status = dok_motion_init(&motion, x, -300, z, (int16_t)nz, 5,
                                     distance < 0 ? -distance : distance);
            if (status != DOK_STATUS_OK) return 5;
            motion.flags = op == 3 ? (distance < 0 ? DOK_INPUT_NATIVE_STRAFE_LEFT : DOK_INPUT_NATIVE_STRAFE_RIGHT) :
                (distance < 0 ? DOK_INPUT_NATIVE_FORWARD : DOK_INPUT_NATIVE_BACKWARD);
            motion.movement_accumulator = 10000;
            status = dok_collision_begin(&collision, world.hero, &motion, 416u, &context);
            if (status == DOK_STATUS_OK) {
                if (distance == 0) {
                    for (k = 0u; k < 3u; ++k) {
                        dok_motion_substep step = {x, -300, z, x, z, 0, (uint16_t)(op - 2), k};
                        dok_collision_move(&context, &step, &x, &z);
                    }
                    motion.x = x; motion.z = z;
                } else if (dok_motion_update(&motion, 416u, &services, &movement) != DOK_STATUS_OK) return 5;
                if (dok_collision_end(&context, &report) != DOK_STATUS_OK) return 5;
            }
            if (!emit((int32_t)status) || !emit(motion.x) || !emit(motion.z) ||
                !emit((int32_t)report.requested) || !emit((int32_t)report.committed) ||
                !emit((int32_t)report.adjusted) || !emit((int32_t)report.rejected) || !emit((int32_t)report.reason_mask)) return 2;
            if (fwrite(world.maps[0], 1u, count, stdout) != count) return 2;
            for (k = 0u; k < 256u; ++k) if (!emit(collision.door_count[k])) return 2;
            continue;
        } else return 2;
        if (!emit((int32_t)clear) || !emit(nx) || !emit(nz)) return 2;
    }
}
