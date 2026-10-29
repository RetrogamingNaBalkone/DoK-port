#include "interaction_cases.h"
#include <stdio.h>
static dok_interaction_test_workspace workspace;
int main(void)
{
    dok_interaction_test_report r;
    int result=dok_interaction_run_cases(&workspace,&r);
    printf("{\"checks\":%lu,\"failures\":%lu,\"first_failed_line\":%lu,"
        "\"door_cases\":%lu,\"floor_cases\":%lu,\"guard_cases\":%lu,\"workspace_bytes\":%lu}\n",
        (unsigned long)r.checks,(unsigned long)r.failures,(unsigned long)r.first_failed_line,
        (unsigned long)r.door_cases,(unsigned long)r.floor_cases,(unsigned long)r.guard_cases,
        (unsigned long)sizeof(workspace));
    return result;
}
