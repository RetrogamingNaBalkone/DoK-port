#ifndef DOK_NDS_DOOR_RENDER_CASES_H
#define DOK_NDS_DOOR_RENDER_CASES_H
#include "dok/door_render.h"
typedef struct dok_door_render_test_workspace {
    dok_geometry_quad source,output,sentinel;
    dok_geometry_image image;
} dok_door_render_test_workspace;
typedef struct dok_door_render_test_report {
    uint32_t checks,failures,first_failed_line,phase_cases,sample_cases,guard_cases;
} dok_door_render_test_report;
_Static_assert(sizeof(dok_door_render_test_workspace)<=1024u,"bounded ARM9 unit workspace");
int dok_door_render_run_cases(dok_door_render_test_workspace *workspace,
    dok_door_render_test_report *report);
#endif
