#ifndef DOK_NDS_TEST_WORLD_EXPORT_H
#define DOK_NDS_TEST_WORLD_EXPORT_H
#include "dok/world.h"
#include <stdint.h>

/* Test observation only; never a save format, package ABI or gameplay API.
 * Callback returns nonzero on success, zero on failure. No allocations. */
typedef int (*dok_test_emit_fn)(void *context, const char *bytes, uint32_t count);
struct dok_level_presentation;
int dok_test_world_export(const dok_level_definition *base,
    const dok_level_definition *effective, const dok_initial_world *world,
    const struct dok_level_presentation *presentation,
    dok_test_emit_fn emit, void *context);
#endif
