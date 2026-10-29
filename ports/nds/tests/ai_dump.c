#include "dok/ai.h"
#include <stdio.h>
int main(void)
{int mode;long long a,b,c,d;while(scanf("%d%lld%lld%lld%lld",&mode,&a,&b,&c,&d)==5){
 if(mode==0){int16_t h;if(dok_ai_heading((int32_t)a,(int32_t)b,&h)!=DOK_STATUS_OK)return 2;printf("%d\n",h);}
 else if(mode==1)printf("%lu\n",(unsigned long)dok_ai_near((int32_t)a,(int32_t)b,(int32_t)c,(int16_t)d));
 else if(mode==2){int16_t counter=(int16_t)a;uint32_t reset=dok_ai_course_step(&counter,(int16_t)b);printf("%d %lu\n",counter,(unsigned long)reset);}
 else return 2;
 }return 0;}
