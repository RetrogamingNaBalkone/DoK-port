#ifndef DOK_NDS_MOTION_CASES_H
#define DOK_NDS_MOTION_CASES_H
#include "dok/camera.h"
typedef struct dok_motion_test_report {
    uint32_t checks, failures, first_failed_line;
    uint32_t motion_cases, camera_cases, prediction_observations;
} dok_motion_test_report;
/* Same strict C11 suite on host/ARM9. No hardware, clock, malloc or stdio. */
int dok_motion_run_cases(dok_motion_test_report *report);
#endif
