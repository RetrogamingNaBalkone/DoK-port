#include "motion_cases.h"
#include <stdio.h>
int main(void)
{
    dok_motion_test_report r;
    int result=dok_motion_run_cases(&r);
    printf("{\"checks\":%lu,\"failures\":%lu,\"first_failed_line\":%lu,"
           "\"motion_cases\":%lu,\"camera_cases\":%lu,\"prediction_observations\":%lu}\n",
           (unsigned long)r.checks,(unsigned long)r.failures,(unsigned long)r.first_failed_line,
           (unsigned long)r.motion_cases,(unsigned long)r.camera_cases,
           (unsigned long)r.prediction_observations);
    return result;
}
