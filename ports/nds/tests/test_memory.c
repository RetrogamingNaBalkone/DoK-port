#include "dok/memory.h"
#include "dok/memory_budget.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t checks;

#define CHECK(expression) do { \
    ++checks; \
    if (!(expression)) { \
        fprintf(stderr, "memory check failed at line %d: %s\n", __LINE__, #expression); \
        return 0; \
    } \
} while (0)

static int same_arena(const dok_arena *left, const dok_arena *right)
{
    return left->base == right->base && left->capacity == right->capacity &&
           left->used == right->used && left->high_water == right->high_water &&
           left->failures == right->failures;
}

static int same_stack(const dok_work_stack *left, const dok_work_stack *right)
{
    return left->base == right->base && left->element_bytes == right->element_bytes &&
           left->capacity == right->capacity && left->depth == right->depth &&
           left->high_water == right->high_water && left->failures == right->failures;
}

static int arena_boundaries(void)
{
    uint8_t storage[259], saved[259];
    dok_arena arena, before;
    void *out = storage;
    memset(storage, 0xA7, sizeof(storage));
    memcpy(saved, storage, sizeof(saved));
    CHECK(dok_arena_init(&arena, storage + 1, 257) == DOK_STATUS_OK);
    CHECK(arena.used == 0 && arena.high_water == 0 && arena.failures == 0);
    CHECK(dok_arena_alloc(&arena, 257, 1, &out) == DOK_STATUS_OK);
    CHECK(out == storage + 1 && arena.used == 257 && arena.high_water == 257);
    CHECK(memcmp(storage, saved, sizeof(storage)) == 0);
    memset(out, 0x41, 257);
    memcpy(saved, storage, sizeof(saved));
    before = arena;
    out = saved;
    CHECK(dok_arena_alloc(&arena, 1, 1, &out) == DOK_STATUS_NO_SPACE);
    CHECK(out == saved && arena.used == before.used && arena.high_water == before.high_water);
    CHECK(arena.failures == before.failures + 1);
    CHECK(memcmp(storage, saved, sizeof(storage)) == 0);
    CHECK(storage[0] == 0xA7 && storage[258] == 0xA7);
    CHECK(dok_arena_reset(&arena) == DOK_STATUS_OK);
    CHECK(arena.used == 0 && arena.high_water == 257 && arena.failures == 1);
    CHECK(memcmp(storage, saved, sizeof(storage)) == 0);
    CHECK(dok_arena_alloc(&arena, 3, 1, &out) == DOK_STATUS_OK);
    CHECK(out == storage + 1 && arena.used == 3 && arena.high_water == 257);
    arena.failures = UINT32_MAX;
    before = arena;
    out = saved;
    CHECK(dok_arena_alloc(&arena, UINT32_MAX, 1, &out) == DOK_STATUS_NO_SPACE);
    CHECK(same_arena(&arena, &before) && out == saved);
    CHECK(dok_arena_init(&arena, NULL, 0) == DOK_STATUS_OK);
    CHECK(dok_arena_alloc(&arena, 1, 1, &out) == DOK_STATUS_NO_SPACE);
    CHECK(arena.used == 0 && arena.high_water == 0 && arena.failures == 1 && out == saved);
    CHECK(dok_arena_reset(&arena) == DOK_STATUS_OK);
    return 1;
}

static int arena_alignment(void)
{
    uint8_t storage[1024], saved[1024];
    uint32_t offset, alignment;
    memset(storage, 0x3D, sizeof(storage));
    memcpy(saved, storage, sizeof(saved));
    for (offset = 0; offset != 64; ++offset) {
        for (alignment = 1; alignment <= 256; alignment *= 2) {
            dok_arena arena;
            void *out;
            uint32_t expected = 3;
            CHECK(dok_arena_init(&arena, storage + offset, 768) == DOK_STATUS_OK);
            CHECK(dok_arena_alloc(&arena, 3, 1, &out) == DOK_STATUS_OK);
            CHECK(out == storage + offset);
            /* Independent oracle: advance to the next divisible address. */
            while ((uintptr_t)(storage + offset + expected) % alignment != 0)
                ++expected;
            CHECK(dok_arena_alloc(&arena, 7, alignment, &out) == DOK_STATUS_OK);
            CHECK(out == storage + offset + expected);
            CHECK((uintptr_t)out % alignment == 0);
            CHECK(arena.used == expected + 7 && arena.high_water == expected + 7);
            CHECK(memcmp(storage, saved, sizeof(storage)) == 0);
        }
    }
    return 1;
}

static int arena_invalid_and_overflow(void)
{
    uint8_t storage[32], saved[32];
    dok_arena arena, before;
    void *out = saved;
    const uint32_t bad_alignment[] = {0, 3, 7, UINT32_MAX};
    size_t i;
    memset(storage, 0x42, sizeof(storage));
    memcpy(saved, storage, sizeof(saved));
    CHECK(dok_arena_init(&arena, storage, sizeof(storage)) == DOK_STATUS_OK);
    CHECK(dok_arena_alloc(&arena, 5, 1, &out) == DOK_STATUS_OK);
    before = arena;
    out = saved;
    CHECK(dok_arena_init(&arena, NULL, 1) == DOK_STATUS_INVALID_ARGUMENT);
    CHECK(same_arena(&arena, &before));
    CHECK(dok_arena_init(&arena, (void *)(UINTPTR_MAX - 7u), 16) == DOK_STATUS_INVALID_ARGUMENT);
    CHECK(same_arena(&arena, &before));
    CHECK(dok_arena_init(NULL, storage, sizeof(storage)) == DOK_STATUS_INVALID_ARGUMENT);
    for (i = 0; i < sizeof(bad_alignment) / sizeof(bad_alignment[0]); ++i) {
        CHECK(dok_arena_alloc(&arena, 1, bad_alignment[i], &out) == DOK_STATUS_INVALID_ARGUMENT);
        CHECK(same_arena(&arena, &before) && out == saved);
    }
    CHECK(dok_arena_alloc(&arena, 0, 1, &out) == DOK_STATUS_INVALID_ARGUMENT);
    CHECK(dok_arena_alloc(&arena, 1, 1, NULL) == DOK_STATUS_INVALID_ARGUMENT);
    CHECK(dok_arena_alloc(NULL, 1, 1, &out) == DOK_STATUS_INVALID_ARGUMENT);
    CHECK(dok_arena_reset(NULL) == DOK_STATUS_INVALID_ARGUMENT);
    CHECK(same_arena(&arena, &before) && out == saved);
    CHECK(dok_arena_alloc(&arena, UINT32_MAX, 1, &out) == DOK_STATUS_NO_SPACE);
    CHECK(arena.used == before.used && arena.high_water == before.high_water && out == saved);
    CHECK(dok_arena_alloc(&arena, UINT32_MAX, UINT32_C(0x80000000), &out) == DOK_STATUS_NO_SPACE);
    CHECK(arena.used == before.used && arena.high_water == before.high_water && out == saved);
    arena.used = arena.capacity + 1;
    before = arena;
    CHECK(dok_arena_alloc(&arena, 1, 1, &out) == DOK_STATUS_INVALID_STATE);
    CHECK(dok_arena_reset(&arena) == DOK_STATUS_INVALID_STATE);
    CHECK(same_arena(&arena, &before) && out == saved);
    CHECK(memcmp(storage, saved, sizeof(storage)) == 0);
    return 1;
}

static int stack_boundaries(void)
{
    uint8_t storage[19], saved[19];
    const uint8_t values[3][5] = {{1, 2, 3, 4, 5}, {11, 12, 13, 14, 15}, {21, 22, 23, 24, 25}};
    uint8_t out[5], untouched[5];
    dok_work_stack stack, before;
    uint32_t i;
    memset(storage, 0xDC, sizeof(storage));
    memset(untouched, 0x8E, sizeof(untouched));
    CHECK(dok_work_stack_init(&stack, storage + 1, 17, 5) == DOK_STATUS_OK);
    CHECK(stack.capacity == 3 && stack.depth == 0 && stack.high_water == 0 && stack.failures == 0);
    memcpy(out, untouched, sizeof(out));
    before = stack;
    CHECK(dok_work_stack_pop(&stack, out) == DOK_STATUS_INVALID_STATE);
    CHECK(same_stack(&stack, &before) && memcmp(out, untouched, sizeof(out)) == 0);
    for (i = 0; i < 3; ++i) {
        CHECK(dok_work_stack_push(&stack, values[i]) == DOK_STATUS_OK);
        CHECK(stack.depth == i + 1 && stack.high_water == i + 1);
    }
    CHECK(storage[0] == 0xDC && storage[16] == 0xDC && storage[17] == 0xDC && storage[18] == 0xDC);
    memcpy(saved, storage, sizeof(saved));
    before = stack;
    CHECK(dok_work_stack_push(&stack, values[0]) == DOK_STATUS_NO_SPACE);
    CHECK(stack.depth == before.depth && stack.high_water == before.high_water && stack.failures == 1);
    CHECK(memcmp(storage, saved, sizeof(storage)) == 0);
    for (i = 3; i > 0; --i) {
        CHECK(dok_work_stack_pop(&stack, out) == DOK_STATUS_OK);
        CHECK(memcmp(out, values[i - 1], sizeof(out)) == 0);
        CHECK(stack.depth == i - 1 && stack.high_water == 3);
    }
    CHECK(dok_work_stack_push(&stack, values[1]) == DOK_STATUS_OK);
    memcpy(saved, storage, sizeof(saved));
    CHECK(dok_work_stack_reset(&stack) == DOK_STATUS_OK);
    CHECK(stack.depth == 0 && stack.high_water == 3 && stack.failures == 1);
    CHECK(memcmp(storage, saved, sizeof(saved)) == 0);
    stack.failures = UINT32_MAX;
    for (i = 0; i < 3; ++i)
        CHECK(dok_work_stack_push(&stack, values[i]) == DOK_STATUS_OK);
    before = stack;
    CHECK(dok_work_stack_push(&stack, values[0]) == DOK_STATUS_NO_SPACE);
    CHECK(same_stack(&stack, &before));
    return 1;
}

static int stack_invalid_and_overlap(void)
{
    uint8_t storage[32], saved[32], out[8];
    dok_work_stack stack, before;
    uint32_t i;
    for (i = 0; i < sizeof(storage); ++i)
        storage[i] = (uint8_t)i;
    CHECK(dok_work_stack_init(&stack, storage, 24, 8) == DOK_STATUS_OK);
    before = stack;
    CHECK(dok_work_stack_init(&stack, NULL, 8, 1) == DOK_STATUS_INVALID_ARGUMENT);
    CHECK(dok_work_stack_init(&stack, storage, sizeof(storage), 0) == DOK_STATUS_INVALID_ARGUMENT);
    CHECK(dok_work_stack_init(&stack, (void *)(UINTPTR_MAX - 3u), 8, 1) == DOK_STATUS_INVALID_ARGUMENT);
    CHECK(same_stack(&stack, &before));
    CHECK(dok_work_stack_push(&stack, NULL) == DOK_STATUS_INVALID_ARGUMENT);
    CHECK(dok_work_stack_pop(&stack, NULL) == DOK_STATUS_INVALID_ARGUMENT);
    CHECK(dok_work_stack_init(NULL, storage, sizeof(storage), 8) == DOK_STATUS_INVALID_ARGUMENT);
    CHECK(dok_work_stack_push(NULL, out) == DOK_STATUS_INVALID_ARGUMENT);
    CHECK(dok_work_stack_pop(NULL, out) == DOK_STATUS_INVALID_ARGUMENT);
    CHECK(dok_work_stack_reset(NULL) == DOK_STATUS_INVALID_ARGUMENT);
    CHECK(same_stack(&stack, &before));
    /* Overlapping push source moves from +1 into slot zero. */
    memcpy(out, storage + 1, sizeof(out));
    CHECK(dok_work_stack_push(&stack, storage + 1) == DOK_STATUS_OK);
    CHECK(memcmp(storage, out, sizeof(out)) == 0);
    /* Overlapping pop writes the value one byte to the right. */
    CHECK(dok_work_stack_pop(&stack, storage + 1) == DOK_STATUS_OK);
    CHECK(memcmp(storage + 1, out, sizeof(out)) == 0 && stack.depth == 0);
    stack.depth = stack.capacity + 1;
    before = stack;
    memcpy(saved, storage, sizeof(saved));
    memset(out, 0x4C, sizeof(out));
    CHECK(dok_work_stack_push(&stack, out) == DOK_STATUS_INVALID_STATE);
    CHECK(dok_work_stack_pop(&stack, out) == DOK_STATUS_INVALID_STATE);
    CHECK(dok_work_stack_reset(&stack) == DOK_STATUS_INVALID_STATE);
    CHECK(same_stack(&stack, &before) && memcmp(storage, saved, sizeof(storage)) == 0);
    for (i = 0; i < sizeof(out); ++i)
        CHECK(out[i] == 0x4C);
    CHECK(dok_work_stack_init(&stack, NULL, 0, 8) == DOK_STATUS_OK);
    CHECK(dok_work_stack_push(&stack, out) == DOK_STATUS_NO_SPACE);
    CHECK(dok_work_stack_pop(&stack, out) == DOK_STATUS_INVALID_STATE);
    CHECK(stack.depth == 0 && stack.high_water == 0 && stack.failures == 1);
    return 1;
}

static int stack_model(void)
{
    uint32_t storage[17], expected[17], depth = 0, maximum = 0, failures = 0;
    uint32_t seed = UINT32_C(0x12D09741), i;
    dok_work_stack stack;
    CHECK(dok_work_stack_init(&stack, storage, sizeof(storage), sizeof(uint32_t)) == DOK_STATUS_OK);
    for (i = 0; i < 10000; ++i) {
        uint32_t out = UINT32_C(0x9ABCDEF0);
        seed = seed * UINT32_C(1664525) + UINT32_C(1013904223);
        if ((seed >> 28) < 10) {
            if (depth == 17) {
                ++failures;
                CHECK(dok_work_stack_push(&stack, &seed) == DOK_STATUS_NO_SPACE);
            } else {
                expected[depth++] = seed;
                CHECK(dok_work_stack_push(&stack, &seed) == DOK_STATUS_OK);
                if (depth > maximum)
                    maximum = depth;
            }
        } else if (depth != 0) {
            CHECK(dok_work_stack_pop(&stack, &out) == DOK_STATUS_OK);
            CHECK(out == expected[--depth]);
        } else {
            CHECK(dok_work_stack_pop(&stack, &out) == DOK_STATUS_INVALID_STATE);
            CHECK(out == UINT32_C(0x9ABCDEF0));
        }
        CHECK(stack.depth == depth && stack.high_water == maximum && stack.failures == failures);
    }
    return 1;
}

int main(void)
{
    if (!arena_boundaries() || !arena_alignment() || !arena_invalid_and_overflow() ||
        !stack_boundaries() || !stack_invalid_and_overlap() || !stack_model())
        return 1;
    printf("N03 memory: %u checks passed (arenas and explicit work stacks; not CPU stacks).\n", (unsigned)checks);
    return 0;
}
