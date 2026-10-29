#include "dok/weapon_overlay.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>
static unsigned char source[1024u*1024u],a[256u*192u],b[256u*192u];
int main(void)
{
    uint32_t seed=170017u,checks=0;
    for(uint32_t i=0;i<sizeof(source);++i){seed=seed*1664525u+1013904223u;source[i]=(unsigned char)(seed>>24u);}
    for(uint32_t i=0;i<6000u;++i){
        seed=seed*1664525u+1013904223u;uint16_t width=(uint16_t)(1u+(seed%1024u));
        seed=seed*1664525u+1013904223u;uint16_t height=(uint16_t)(1u+(seed%1024u));
        int32_t left=(int32_t)(seed%4096u)-2048;seed=seed*1664525u+1013904223u;int32_t top=(int32_t)(seed%4096u)-2048;
        if(i<8u){left=(i&1u)?INT32_MIN:INT32_MAX;top=(i&2u)?INT32_MIN:INT32_MAX;}
        dok_weapon_view view={{1,0,0,0,width,height},left,top,i%17u!=0};
        memset(a,0x73,sizeof(a));memset(b,0x73,sizeof(b));
        dok_status x=dok_weapon_overlay(&view,source,1024,sizeof(source),a,sizeof(a));
        dok_status y=dok_weapon_overlay_clipped(&view,source,1024,sizeof(source),b,sizeof(b));
        ++checks;if(x!=y||memcmp(a,b,sizeof(a))){fprintf(stderr,"weapon_overlay_fast:%lu\n",(unsigned long)i);return 1;}
    }
    printf("{\"checks\":%lu,\"failures\":0}\n",(unsigned long)checks);return 0;
}
