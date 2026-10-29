#include "dok/combat.h"
#include <stddef.h>
#include <string.h>
dok_status dok_weapon_overlay(const dok_weapon_view *view,const unsigned char *pixels,
    uint16_t pitch,uint32_t source_bytes,unsigned char *out,uint32_t bytes)
{
    uint32_t x,y;
    if(!view||!out||bytes<256u*192u)return DOK_STATUS_INVALID_ARGUMENT;
    if(view->visible&&(!pixels||pitch<view->frame.width||!view->frame.width||!view->frame.height||
       (uint32_t)pitch*view->frame.height>source_bytes))return DOK_STATUS_INVALID_ARGUMENT;
    memset(out,0,256u*154u);
    if(!view->visible)return DOK_STATUS_OK;
    for(y=0;y<154u;++y){int64_t sy=(int64_t)y*25/24-view->top;if(sy<0||sy>=view->frame.height)continue;
        for(x=0;x<256u;++x){int64_t sx=(int64_t)x*5/4-view->left;if(sx>=0&&sx<view->frame.width)
            out[y*256u+x]=pixels[(uint32_t)sy*pitch+(uint32_t)sx];}}
    return DOK_STATUS_OK;
}
