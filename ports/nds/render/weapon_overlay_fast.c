#include "dok/weapon_overlay.h"
#include <string.h>
static int64_t ceil_div(int64_t n,int64_t d){return n>=0?(n+d-1)/d:n/d;}
dok_status dok_weapon_overlay_clipped(const dok_weapon_view *v,const unsigned char *pixels,uint16_t pitch,uint32_t source_bytes,unsigned char *out,uint32_t bytes)
{
    if(!v||!out||bytes<256u*192u)return DOK_STATUS_INVALID_ARGUMENT;
    if(v->visible&&(!pixels||pitch<v->frame.width||!v->frame.width||!v->frame.height||(uint32_t)pitch*v->frame.height>source_bytes))return DOK_STATUS_INVALID_ARGUMENT;
    memset(out,0,256u*154u);if(!v->visible)return DOK_STATUS_OK;
    int64_t left=ceil_div((int64_t)v->left*4,5),right=ceil_div(((int64_t)v->left+v->frame.width)*4,5);
    int64_t top=ceil_div((int64_t)v->top*24,25),bottom=ceil_div(((int64_t)v->top+v->frame.height)*24,25);
    if(left<0)left=0;if(right>256)right=256;if(top<0)top=0;if(bottom>154)bottom=154;
    if(left>=right||top>=bottom)return DOK_STATUS_OK;
    /* The nonempty clipping proof bounds left/top to the source dimensions,
     * so these subtractions fit int32 even for arbitrary int32 view inputs. */
    for(uint32_t y=(uint32_t)top;y<(uint32_t)bottom;++y){uint32_t sy=(uint32_t)((int32_t)(y*25u/24u)-v->top);
        for(uint32_t x=(uint32_t)left;x<(uint32_t)right;++x){uint32_t sx=(uint32_t)((int32_t)(x*5u/4u)-v->left);out[y*256u+x]=pixels[sy*pitch+sx];}}
    return DOK_STATUS_OK;
}
