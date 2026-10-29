#ifndef DOK_MEMORY_H
#define DOK_MEMORY_H

#include <stdint.h>
#include "dok/contracts.h"

/* Caller owns a real writable backing allocation of the stated extent and
 * its lifetime. Metadata objects and output pointers must not alias metadata.
 * An integer-range check cannot prove that arbitrary pointers are accessible.
 * No allocations from the C heap.
 * These structs are native C types, never file formats or DOS layouts.
 * Failure preserves backing bytes, cursor/depth, high-water and output.
 * Only NO_SPACE may increment the saturating diagnostic failures counter.
 * Reset invalidates every borrowed pointer and retains lifetime high-water.
 * Raw stale C pointers cannot be detected; their leases are caller-managed.
 * Byte arrays are valid byte storage; typed objects additionally require
 * storage/effective-type/alignment rules of native C11, never a DOS layout.
 */
typedef struct dok_arena {
    uint8_t *base;
    uint32_t capacity;
    uint32_t used;
    uint32_t high_water;
    uint32_t failures;
} dok_arena;

dok_status dok_arena_init(dok_arena *arena, void *backing, uint32_t bytes);
dok_status dok_arena_alloc(dok_arena *arena, uint32_t bytes,
                           uint32_t alignment, void **out);
dok_status dok_arena_reset(dok_arena *arena);

/* Explicit bounded algorithm/work stack, not the machine's C call stack.
 * Element values are copied; any incomplete trailing slot is unavailable.
 * Push/pop allow overlap with backing storage (memmove semantics).
 */
typedef struct dok_work_stack {
    uint8_t *base;
    uint32_t element_bytes;
    uint32_t capacity;
    uint32_t depth;
    uint32_t high_water;
    uint32_t failures;
} dok_work_stack;

dok_status dok_work_stack_init(dok_work_stack *stack, void *backing,
                               uint32_t backing_bytes, uint32_t element_bytes);
dok_status dok_work_stack_push(dok_work_stack *stack, const void *value);
dok_status dok_work_stack_pop(dok_work_stack *stack, void *out);
dok_status dok_work_stack_reset(dok_work_stack *stack);

#endif
