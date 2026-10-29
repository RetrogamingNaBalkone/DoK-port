#include "combat_cases.h"
#include <stdio.h>
static dok_combat_cases_workspace workspace;
int main(int argc,char **argv)
{dok_combat_cases_report r;int failed;if(argc!=2)return 2;failed=dok_combat_cases(&workspace,&r,argv[1]);
 printf("{\"checks\":%lu,\"failures\":%lu,\"line\":%lu,\"scenarios\":%lu,\"callbacks\":%lu,\"trace_hash\":%lu,\"workspace_bytes\":%lu,\"combat_bytes\":%lu}\n",
  (unsigned long)r.checks,(unsigned long)r.failures,(unsigned long)r.line,(unsigned long)r.scenarios,(unsigned long)r.callbacks,(unsigned long)r.trace_hash,(unsigned long)sizeof(workspace),(unsigned long)sizeof(workspace.combat));return failed;}
