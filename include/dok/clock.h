#ifndef DOK_CLOCK_H
#define DOK_CLOCK_H
#include <stdint.h>
/* Crystal approximation 1193182 Hz / native DIV 119.
 * Counter input: GBA cycles / 64, modulo 2^32; poll within one wrap. */
typedef struct dok_clock { uint32_t previous, ticks, remainder; } dok_clock;
void dok_clock_init(dok_clock *, uint32_t counter, uint32_t ticks);
uint32_t dok_clock_advance(dok_clock *, uint32_t counter);
/* Fixed native-tick lattice. Debt is retained, caller bounds work per frame. */
typedef struct dok_steps { uint32_t sampled, now, debt, quantum; } dok_steps;
int dok_steps_init(dok_steps *, uint32_t now, uint32_t quantum);
void dok_steps_sample(dok_steps *, uint32_t now);
int dok_steps_next(dok_steps *);
#endif
