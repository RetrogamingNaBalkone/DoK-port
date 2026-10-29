#include "collision_cases.h"
#include <stdio.h>
static dok_collision_test_workspace workspace;
int main(void)
{
    dok_collision_test_report r;
    int result=dok_collision_run_cases(&workspace,&r);
    printf("{\"checks\":%lu,\"failures\":%lu,\"first_failed_line\":%lu,"
        "\"native_cases\":%lu,\"transaction_cases\":%lu,\"camera_cases\":%lu}\n",
        (unsigned long)r.checks,(unsigned long)r.failures,(unsigned long)r.first_failed_line,
        (unsigned long)r.native_cases,(unsigned long)r.transaction_cases,(unsigned long)r.camera_cases);
    return result;
}
