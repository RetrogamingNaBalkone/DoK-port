#include "dok/ai.h"
#include <stdio.h>
int main(void)
{int mode;long long a,b,c,d;while(scanf("%d%lld%lld%lld%lld",&mode,&a,&b,&c,&d)==5){
 if(mode==0)printf("%lu\n",(unsigned long)dok_ai_burst_due((uint32_t)a,(int16_t)b));
 else if(mode==1)printf("%d\n",dok_ai_damage_word((int16_t)a,(int16_t)b));else return 2;
 }return 0;}
