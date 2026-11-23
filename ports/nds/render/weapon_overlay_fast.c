#include "dok/weapon_overlay.h"
#include <string.h>

typedef struct overlay_rect {
    uint32_t left,right,top,bottom;
} overlay_rect;

static int64_t ceil_div(int64_t n,int64_t d){return n>=0?(n+d-1)/d:n/d;}

static overlay_rect clipped_rect(const dok_weapon_view *v)
{
    if(!v->visible)return (overlay_rect){0};
    int64_t left=ceil_div((int64_t)v->left*4,5);
    int64_t right=ceil_div(((int64_t)v->left+v->frame.width)*4,5);
    int64_t top=ceil_div((int64_t)v->top*24,25);
    int64_t bottom=ceil_div(((int64_t)v->top+v->frame.height)*24,25);
    if(left<0)left=0;if(right>256)right=256;if(top<0)top=0;if(bottom>154)bottom=154;
    if(left>=right||top>=bottom)return (overlay_rect){0};
    return (overlay_rect){(uint32_t)left,(uint32_t)right,(uint32_t)top,(uint32_t)bottom};
}

dok_status dok_weapon_overlay_rows(const dok_weapon_view *v,const dok_weapon_view *previous,
    const unsigned char *pixels,uint16_t pitch,uint32_t source_bytes,
    unsigned char *out,uint32_t bytes,uint32_t *first,uint32_t *end)
{
    if(!v||!out||!first||!end||bytes<256u*192u)return DOK_STATUS_INVALID_ARGUMENT;
    if(v->visible&&(!pixels||pitch<v->frame.width||!v->frame.width||!v->frame.height||
       (uint32_t)pitch*v->frame.height>source_bytes))return DOK_STATUS_INVALID_ARGUMENT;
    overlay_rect rect=clipped_rect(v);
    uint32_t top=0,bottom=154;
    if(previous){
        overlay_rect old=clipped_rect(previous);
        top=rect.top;bottom=rect.bottom;
        if(old.bottom){
            if(!bottom||old.top<top)top=old.top;
            if(old.bottom>bottom)bottom=old.bottom;
        }
    }
    if(top<bottom)memset(out+top*256u,0,(bottom-top)*256u);
    /* Clipping a nonempty rect bounds the source-coordinate subtraction. */
    for(uint32_t y=rect.top;y<rect.bottom;++y){
        uint32_t sy=(uint32_t)((int32_t)(y*25u/24u)-v->top);
        for(uint32_t x=rect.left;x<rect.right;++x){
            uint32_t sx=(uint32_t)((int32_t)(x*5u/4u)-v->left);
            out[y*256u+x]=pixels[sy*pitch+sx];
        }
    }
    *first=top;*end=bottom;
    return DOK_STATUS_OK;
}

dok_status dok_weapon_overlay_clipped(const dok_weapon_view *v,const unsigned char *pixels,
    uint16_t pitch,uint32_t source_bytes,unsigned char *out,uint32_t bytes)
{
    uint32_t first,end;
    return dok_weapon_overlay_rows(v,NULL,pixels,pitch,source_bytes,out,bytes,&first,&end);
}
