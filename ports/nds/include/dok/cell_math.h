#ifndef DOK_NDS_CELL_MATH_H
#define DOK_NDS_CELL_MATH_H
#include <stdint.h>
#ifndef N23_NATIVE_DIV480
#define N23_NATIVE_DIV480 1
#endif
/* The three original worlds use side480. Exposing that constant lets ARM9
 * replace software IDIV with a multiply while keeping signed truncation and
 * every nonstandard positive cell size accepted by the public APIs. */
static inline int32_t dok_cell_quotient(int32_t value,int32_t side)
{
#if N23_NATIVE_DIV480
    if(side==480)return value/480;
#endif
    return value/side;
}
static inline int32_t dok_cell_remainder(int32_t value,int32_t side)
{
#if N23_NATIVE_DIV480
    if(side==480)return value%480;
#endif
    return value%side;
}
#endif
