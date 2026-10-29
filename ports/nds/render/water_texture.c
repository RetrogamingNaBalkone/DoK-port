#include "dok/water_texture.h"
#include <string.h>
uint32_t dok_water_uv_shift(uint32_t phase,uint32_t size,uint32_t negative)
{uint32_t n=size?(size/18u*phase)%size:0;return negative&&n?size-n:n;}
dok_status dok_water_texture(const unsigned char *source,uint16_t width,uint16_t height,uint16_t pitch,uint32_t axis,unsigned char *out,uint32_t bytes)
{
    if(!source||!out||width!=96u||height!=96u||pitch!=128u||axis>1u||bytes<32768u)return DOK_STATUS_INVALID_ARGUMENT;
    memset(out,0,32768u);
    if(!axis)for(uint32_t y=0;y<96u;++y){memcpy(out+y*256u,source+y*128u,96u);memcpy(out+y*256u+96u,source+y*128u,96u);}
    else for(uint32_t y=0;y<192u;++y)memcpy(out+y*128u,source+(y%96u)*128u,96u);
    return DOK_STATUS_OK;
}
