#include "ai_combat_cases.h"
#include <stdio.h>
#include <string.h>
static dok_ai_combat_cases_workspace workspace;
static void trace(void *context,const dok_ai_combat_trace *t)
{
    FILE *f=context;int32_t v[33];memcpy(v,t,sizeof(*t));
    fputc('[',f);for(uint32_t i=0;i<sizeof(*t)/4u;++i)fprintf(f,"%s%ld",i?",":"",(long)v[i]);fputs("]\n",f);
}
int main(int argc,char **argv)
{
    dok_ai_combat_cases_report r;FILE *f=NULL;if(argc<2||argc>3)return 2;
    if(argc==3){f=fopen(argv[2],"wb");if(!f)return 2;workspace.trace=trace;workspace.trace_context=f;}
    int result=dok_ai_combat_cases(&workspace,&r,argv[1]);if(f)fclose(f);
    printf("{\"checks\":%lu,\"failures\":%lu,\"line\":%lu,\"scenarios\":%lu,\"callbacks\":%lu,\"trace_hash\":%lu,\"groups\":%lu,\"placed_definitions\":%lu,\"deaths\":%lu,\"melee\":%lu,\"ranged\":%lu,\"trace_count\":%lu,\"negative_intervals\":%lu}\n",
      (unsigned long)r.checks,(unsigned long)r.failures,(unsigned long)r.line,(unsigned long)r.scenarios,(unsigned long)r.callbacks,(unsigned long)r.trace_hash,
      (unsigned long)r.groups,(unsigned long)r.placed_definitions,(unsigned long)r.deaths,(unsigned long)r.melee,(unsigned long)r.ranged,(unsigned long)r.trace_count,(unsigned long)r.negative_intervals);return result;
}
