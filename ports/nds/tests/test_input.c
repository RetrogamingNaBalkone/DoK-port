#include "input_cases.h"

#include <stdio.h>

static dok_input_buffer buffer;

int main(void)
{
    dok_input_test_report report;
    int result = dok_input_run_cases(&report, &buffer);
    printf("{\"checks\":%lu,\"failures\":%lu,\"first_failed_line\":%lu,"
           "\"mapping_masks\":%lu,\"ordered_samples\":%lu,"
           "\"suppression_masks\":%lu,\"integration_cases\":%lu,\"buffer_bytes\":%lu}\n",
           (unsigned long)report.checks, (unsigned long)report.failures,
           (unsigned long)report.first_failed_line,
           (unsigned long)report.mapping_masks,
           (unsigned long)report.ordered_samples,
           (unsigned long)report.suppression_masks,
           (unsigned long)report.integration_cases,
           (unsigned long)sizeof(buffer));
    return result;
}
