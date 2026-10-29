#include "interaction_router_cases.h"
#include <stdio.h>
static dok_interaction_router_workspace workspace;
int main(void)
{
    dok_interaction_router_report r;
    int result=dok_interaction_router_run_cases(&workspace,&r);
    printf("{\"checks\":%lu,\"failures\":%lu,\"first_failed_line\":%lu,\"pulse_cases\":%lu,"
        "\"use_cases\":%lu,\"pause_cases\":%lu,\"workspace_bytes\":%lu}\n",
        (unsigned long)r.checks,(unsigned long)r.failures,(unsigned long)r.first_failed_line,
        (unsigned long)r.pulse_cases,(unsigned long)r.use_cases,(unsigned long)r.pause_cases,
        (unsigned long)sizeof(workspace));
    return result;
}
