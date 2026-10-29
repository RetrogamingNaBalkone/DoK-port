#ifndef DOK_NDS_TIME_CASES_H
#define DOK_NDS_TIME_CASES_H

#include "dok/events.h"

typedef struct dok_time_test_report {
    uint32_t checks;
    uint32_t failures;
    uint32_t first_failed_line;
    uint32_t clock_cases;
    uint32_t event_cases;
} dok_time_test_report;

/* Destructive, shared host/ARM9 cases; bank is caller-owned events storage.
 * No malloc, stdio, hardware or hidden static owner bank. Returns failures. */
int dok_time_run_cases(dok_time_test_report *report, dok_event_bank *bank);

#endif
