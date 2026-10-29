#include "items_cases.h"
#include <stdio.h>
static dok_items_cases_workspace workspace;
int main(int argc,char **argv)
{ dok_items_cases_report r;int failed;if(argc!=2)return 2;
  failed=dok_items_cases(&workspace,&r,argv[1]);
  printf("{\"checks\":%lu,\"failures\":%lu,\"line\":%lu,\"placements\":%lu,\"notifications\":%lu,\"workspace_bytes\":%lu}\n",
   (unsigned long)r.checks,(unsigned long)r.failures,(unsigned long)r.line,(unsigned long)r.placements,
   (unsigned long)r.notifications,(unsigned long)sizeof(workspace));return failed; }
