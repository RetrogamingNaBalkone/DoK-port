#include "dok/memory.h"

#include <stddef.h>
#include <string.h>

static int valid_range(const void *base, uint32_t bytes)
{
    return (base != NULL || bytes == 0) &&
           (uintptr_t)bytes <= UINTPTR_MAX - (uintptr_t)base;
}

static void count_failure(uint32_t *counter)
{
    if (*counter != UINT32_MAX)
        ++*counter;
}

static int valid_arena(const dok_arena *arena)
{
    return arena != NULL && valid_range(arena->base, arena->capacity) &&
           arena->used <= arena->high_water &&
           arena->high_water <= arena->capacity;
}

dok_status dok_arena_init(dok_arena *arena, void *backing, uint32_t bytes)
{
    dok_arena fresh;
    if (arena == NULL || !valid_range(backing, bytes))
        return DOK_STATUS_INVALID_ARGUMENT;
    fresh.base = backing;
    fresh.capacity = bytes;
    fresh.used = 0;
    fresh.high_water = 0;
    fresh.failures = 0;
    *arena = fresh;
    return DOK_STATUS_OK;
}

dok_status dok_arena_alloc(dok_arena *arena, uint32_t bytes,
                           uint32_t alignment, void **out)
{
    uintptr_t current;
    uint32_t padding, remaining, end;
    void *result;
    if (arena == NULL || out == NULL || bytes == 0 || alignment == 0 ||
        (alignment & (alignment - 1u)) != 0)
        return DOK_STATUS_INVALID_ARGUMENT;
    if (!valid_arena(arena))
        return DOK_STATUS_INVALID_STATE;
    current = (uintptr_t)arena->base + arena->used;
    padding = (uint32_t)((0u - current) & (uintptr_t)(alignment - 1u));
    remaining = arena->capacity - arena->used;
    if (padding > remaining || bytes > remaining - padding) {
        count_failure(&arena->failures);
        return DOK_STATUS_NO_SPACE;
    }
    end = arena->used + padding + bytes;
    result = arena->base + arena->used + padding;
    arena->used = end;
    if (end > arena->high_water)
        arena->high_water = end;
    *out = result;
    return DOK_STATUS_OK;
}

dok_status dok_arena_reset(dok_arena *arena)
{
    if (arena == NULL)
        return DOK_STATUS_INVALID_ARGUMENT;
    if (!valid_arena(arena))
        return DOK_STATUS_INVALID_STATE;
    arena->used = 0;
    return DOK_STATUS_OK;
}

static int valid_stack(const dok_work_stack *stack)
{
    return stack != NULL && stack->element_bytes != 0 &&
           stack->capacity <= UINT32_MAX / stack->element_bytes &&
           valid_range(stack->base, stack->capacity * stack->element_bytes) &&
           stack->depth <= stack->high_water &&
           stack->high_water <= stack->capacity;
}

dok_status dok_work_stack_init(dok_work_stack *stack, void *backing,
                               uint32_t backing_bytes, uint32_t element_bytes)
{
    dok_work_stack fresh;
    if (stack == NULL || element_bytes == 0 || !valid_range(backing, backing_bytes))
        return DOK_STATUS_INVALID_ARGUMENT;
    fresh.base = backing;
    fresh.element_bytes = element_bytes;
    fresh.capacity = backing_bytes / element_bytes;
    fresh.depth = 0;
    fresh.high_water = 0;
    fresh.failures = 0;
    *stack = fresh;
    return DOK_STATUS_OK;
}

dok_status dok_work_stack_push(dok_work_stack *stack, const void *value)
{
    if (stack == NULL || value == NULL)
        return DOK_STATUS_INVALID_ARGUMENT;
    if (!valid_stack(stack))
        return DOK_STATUS_INVALID_STATE;
    if (stack->depth == stack->capacity) {
        count_failure(&stack->failures);
        return DOK_STATUS_NO_SPACE;
    }
    memmove(stack->base + stack->depth * stack->element_bytes,
            value, stack->element_bytes);
    ++stack->depth;
    if (stack->depth > stack->high_water)
        stack->high_water = stack->depth;
    return DOK_STATUS_OK;
}

dok_status dok_work_stack_pop(dok_work_stack *stack, void *out)
{
    if (stack == NULL || out == NULL)
        return DOK_STATUS_INVALID_ARGUMENT;
    if (!valid_stack(stack))
        return DOK_STATUS_INVALID_STATE;
    if (stack->depth == 0)
        return DOK_STATUS_INVALID_STATE;
    memmove(out, stack->base + (stack->depth - 1u) * stack->element_bytes,
            stack->element_bytes);
    --stack->depth;
    return DOK_STATUS_OK;
}

dok_status dok_work_stack_reset(dok_work_stack *stack)
{
    if (stack == NULL)
        return DOK_STATUS_INVALID_ARGUMENT;
    if (!valid_stack(stack))
        return DOK_STATUS_INVALID_STATE;
    stack->depth = 0;
    return DOK_STATUS_OK;
}
