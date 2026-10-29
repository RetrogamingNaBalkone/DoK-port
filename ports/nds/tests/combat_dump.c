#include "dok/combat.h"
#include <stdio.h>
int main(void)
{
    int mode;long long a,b,c,d;while(scanf("%d%lld%lld%lld%lld",&mode,&a,&b,&c,&d)==5){
        if(mode==0){dok_weapon_runtime w={(int16_t)a,(int16_t)b,(int16_t)c,0};uint32_t cancel;
            if(dok_weapon_step(&w,(uint32_t)d,&cancel)!=DOK_STATUS_OK)return 2;
            printf("%d %d %d %lu\n",w.state,w.shift,w.cursor,(unsigned long)cancel);
        }else if(mode==1){dok_weapon_runtime w={(int16_t)a,0,0,0};uint32_t ready=dok_weapon_ready(&w);printf("%lu %d\n",(unsigned long)ready,w.state);}
        else if(mode==2)printf("%d\n",dok_projectile_decay((int16_t)a,(int16_t)b));
        else if(mode==3){int32_t distance;int16_t quarter;if(dok_projectile_distance((int16_t)a,(uint32_t)b,(int16_t)c,&distance,&quarter)!=DOK_STATUS_OK)return 2;printf("%ld %d\n",(long)distance,quarter);}
        else return 2;
    }return 0;
}
