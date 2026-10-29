#ifndef DOK_NDS_INPUT_CASES_H
#define DOK_NDS_INPUT_CASES_H

#include "dok/input.h"

typedef struct dok_input_test_report {
    uint32_t checks;
    uint32_t failures;
    uint32_t first_failed_line;
    uint32_t mapping_masks;
    uint32_t ordered_samples;
    uint32_t suppression_masks;
    uint32_t integration_cases;
} dok_input_test_report;

/* Shared strict-C11 host/ARM9 suite, destructive to the caller's input buffer.
 * No SDK, stdio, heap or hidden buffer. Returns nonzero on any failure. */
int dok_input_run_cases(dok_input_test_report *report, dok_input_buffer *buffer);

#endif
