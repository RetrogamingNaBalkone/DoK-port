#include "runtime.h"
#include "old-runtime.h"
#include "dok/weapon_overlay.h"
#include <stdio.h>
#include <string.h>
#include <stddef.h>
_Static_assert(sizeof(NdsUiRuntime)==sizeof(OldUiRuntime),"use existing tail padding");
_Static_assert(offsetof(NdsUiRuntime,overlay_blank)==offsetof(OldUiRuntime,lives)+2u,"blank flag follows lives");
_Static_assert(offsetof(NdsUiRuntime,overlay_blank)+2u<=sizeof(OldUiRuntime),"blank flag in padding");
extern int old_overlay(const dok_weapon_view *,const unsigned char *,uint16_t *,unsigned,unsigned char *,unsigned char *,unsigned *);
extern int new_overlay(const dok_weapon_view *,const unsigned char *,uint16_t *,unsigned,unsigned char *,unsigned char *,unsigned *);
static unsigned char a[49152],b[49152],sa[131072],sb[131072],pixels[64];
int main(void)
{
    unsigned frames=0,skips=0,visible=0,invalidations=0;
    for(unsigned audit=0;audit<4;++audit){
        uint16_t old_blank=0,blank=0;memset(a,0x59,sizeof(a));memcpy(b,a,sizeof(a));
        for(unsigned n=0;n<1024;++n){
            if(n%31u==0){blank=0;memset(a,(int)(n&255u),sizeof(a));memcpy(b,a,sizeof(a));++invalidations;}
            dok_weapon_view view={0};view.visible=(n%17u==8u||n%17u==9u);
            view.frame.width=8;view.frame.height=8;view.left=(int32_t)(n%340u)-10;view.top=(int32_t)(n%170u)-10;
            for(unsigned k=0;k<64;++k)pixels[k]=(unsigned char)(n+k);
            unsigned x=0,y=0;uint16_t before=blank;
            if(!old_overlay(&view,pixels,&old_blank,audit,sa,a,&x)||!new_overlay(&view,pixels,&blank,audit,sb,b,&y))return 1;
            if(memcmp(a,b,sizeof(a))||x!=1u||y!=(audit||view.visible||!before?1u:0u))return 2;
            if(blank!=(uint16_t)!view.visible)return 3;
            skips+=!y;visible+=view.visible;++frames;
        }
    }
    if(!skips||!visible)return 4;
    printf("{\"passed\":true,\"frames\":%u,\"hidden_skips\":%u,\"visible_full_paths\":%u,\"invalidations\":%u,\"ui_bytes\":%u}\n",frames,skips,visible,invalidations,(unsigned)sizeof(NdsUiRuntime));
    return 0;
}
