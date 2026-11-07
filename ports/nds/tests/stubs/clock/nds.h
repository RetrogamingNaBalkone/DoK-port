#ifndef DOK_TEST_CLOCK_NDS_H
#define DOK_TEST_CLOCK_NDS_H
#include <stdint.h>
#define BUS_CLOCK 33513982u
uint64_t systemCounterGetTicks(void);
void systemCounterSetup(void);
#endif
