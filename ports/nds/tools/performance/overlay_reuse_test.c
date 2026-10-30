#include "dok/overlay_reuse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned char source[65536],moved[65537],reference[49152],retained[49152];
static uint32_t seed=230807u,checks,hits,draws,visible_cases,visible_hits;
static uint32_t random32(void){seed=seed*1664525u+1013904223u;return seed;}
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main(void)
{
    dok_overlay_key key={0},previous={0};uint32_t valid=0;
    memset(reference,0xa5,sizeof(reference));memset(retained,0xa5,sizeof(retained));
    for(uint32_t step=0;step<6000u;++step){
        if(step%31u==0u){
            ++key.serial;key.view.frame.image=step+1u;
            for(uint32_t i=0;i<sizeof(source);++i)source[i]=(unsigned char)random32();
            memcpy(moved+1,source,sizeof(source));
        }
        if(step%8u==0u){
            key.view.visible=random32()>>31u;key.view.frame.width=(uint16_t)(1u+random32()%256u);
            key.view.frame.height=(uint16_t)(1u+random32()%256u);key.pitch=key.view.frame.width;
            key.bytes=(uint32_t)key.pitch*key.view.frame.height;
            key.view.left=(int32_t)(random32()%1024u)-512;key.view.top=(int32_t)(random32()%768u)-384;
            key.view.frame.interval=(int16_t)random32();key.view.frame.parameter_a=(int16_t)random32();key.view.frame.parameter_b=(int16_t)random32();
        }
        if(step%37u==0u){valid=0;memset(retained,0x77,256u*154u);}
        for(uint32_t i=256u*154u;i<sizeof(reference);++i)reference[i]=retained[i]=(unsigned char)(step+i);
        const unsigned char *pixels=step&1u?source:moved+1;
        visible_cases+=key.view.visible!=0;
        CHECK(dok_weapon_overlay(&key.view,pixels,key.pitch,key.bytes,reference,sizeof(reference))==DOK_STATUS_OK);
        if(!valid||!dok_overlay_key_equal(&key,&previous)){
            CHECK(dok_weapon_overlay_clipped(&key.view,pixels,key.pitch,key.bytes,retained,sizeof(retained))==DOK_STATUS_OK);
            previous=key;valid=1;++draws;
        }else{++hits;visible_hits+=key.view.visible!=0;}
        CHECK(!memcmp(reference,retained,sizeof(reference)));
    }
    /* Each input field belongs to the conservative key; padding does not. */
    dok_overlay_key changed=key;
#define DIFFER(field) do{changed=key;++changed.field;CHECK(!dok_overlay_key_equal(&key,&changed));}while(0)
    DIFFER(serial);DIFFER(bytes);DIFFER(pitch);DIFFER(view.visible);DIFFER(view.left);DIFFER(view.top);
    DIFFER(view.frame.image);DIFFER(view.frame.width);DIFFER(view.frame.height);DIFFER(view.frame.interval);
    DIFFER(view.frame.parameter_a);DIFFER(view.frame.parameter_b);
#undef DIFFER
    CHECK(visible_cases>1000u&&visible_hits>1000u);
    printf("{\"passed\":true,\"cases\":6000,\"visible_cases\":%u,\"visible_hits\":%u,\"checks\":%u,\"hits\":%u,\"draws\":%u,\"compared_bytes\":294912000}\n",visible_cases,visible_hits,checks,hits,draws);
    return 0;
}
