#include "time_cases.h"

#include <stdio.h>

static dok_event_bank bank;

int main(void)
{
    dok_time_test_report report;
    int result = dok_time_run_cases(&report, &bank);
    printf("{\"checks\":%lu,\"failures\":%lu,\"first_failed_line\":%lu,"
           "\"clock_cases\":%lu,\"event_cases\":%lu,\"bank_bytes\":%lu}\n",
           (unsigned long)report.checks, (unsigned long)report.failures,
           (unsigned long)report.first_failed_line,
           (unsigned long)report.clock_cases, (unsigned long)report.event_cases,
           (unsigned long)sizeof(bank));
    return result;
}
