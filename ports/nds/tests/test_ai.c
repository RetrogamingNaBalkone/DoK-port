#include "ai_cases.h"
#include <stdio.h>
static dok_ai_cases_workspace workspace;
int main(int argc,char **argv)
{dok_ai_cases_report r;int result;if(argc!=2)return 2;result=dok_ai_cases(&workspace,&r,argv[1]);
 printf("{\"checks\":%lu,\"failures\":%lu,\"line\":%lu,\"scenarios\":%lu,\"callbacks\":%lu,\"trace_hash\":%lu,\"considerations\":%lu,\"motion_callbacks\":%lu,\"course_trials\":%lu,\"ai_bytes\":%lu,\"stress_callbacks\":%lu}\n",
 (unsigned long)r.checks,(unsigned long)r.failures,(unsigned long)r.line,(unsigned long)r.scenarios,(unsigned long)r.callbacks,(unsigned long)r.trace_hash,
 (unsigned long)r.considerations,(unsigned long)r.motion_callbacks,(unsigned long)r.course_trials,(unsigned long)sizeof(workspace.ai),(unsigned long)r.stress_callbacks);return result;}
