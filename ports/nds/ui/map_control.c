#include "dok/map_control.h"
#include <string.h>
const uint8_t dok_map_scales[5]={2,3,4,6,8};
static int32_t clamp_axis(int32_t center,uint32_t cells,uint32_t pixels,uint32_t scale)
{
    if(cells*scale<=pixels)return (int32_t)(cells*32768u);
    int32_t near=(int32_t)((pixels*32768u+scale-1u)/scale);
    int32_t far=(int32_t)(cells*65536u)-near;
    return center<near?near:center>far?far:center;
}
static void bounds(dok_map_control *s)
{
    uint32_t scale=dok_map_scales[s->zoom];
    s->center_x_q16=clamp_axis(s->center_x_q16,s->width,256u,scale);
    s->center_z_q16=clamp_axis(s->center_z_q16,s->length,160u,scale);
}
static void follow(dok_map_control *s,int16_t x,int16_t z)
{
    s->center_x_q16=((int32_t)s->width-x)*65536+s->follow_x_q16;
    s->center_z_q16=(int32_t)z*65536+s->follow_z_q16;
}
void dok_map_control_init(dok_map_control *s,uint16_t w,uint16_t h,int16_t x,int16_t z)
{
    memset(s,0,sizeof(*s));s->width=w;s->length=h;s->zoom=1;s->follow=1;
    follow(s,x,z);bounds(s);
}
void dok_map_control_step(dok_map_control *s,dok_map_touch t,int16_t x,int16_t z)
{
    dok_map_control old=*s;
    if(t.paused){s->capture=0;s->blocked=t.held;s->held=t.held;}
    else if(!t.held){s->capture=0;s->blocked=0;s->held=0;}
    else if(!s->blocked){
        if(t.x<0||t.x>=256||t.y<0||t.y>=192){s->capture=0;s->blocked=1;s->held=1;}
        else if(!s->held){
            s->held=1;s->last_x=t.x;s->last_y=t.y;++s->starts;
            if(t.y<160)s->capture=DOK_MAP_CAPTURE_MAP;
            else {
                s->capture=t.x<85?DOK_MAP_CAPTURE_MINUS:t.x<170?DOK_MAP_CAPTURE_PLUS:DOK_MAP_CAPTURE_CENTER;
                uint8_t old_zoom=s->zoom;
                if(s->capture==DOK_MAP_CAPTURE_MINUS&&s->zoom)--s->zoom;
                else if(s->capture==DOK_MAP_CAPTURE_PLUS&&s->zoom<4u)++s->zoom;
                else if(s->capture==DOK_MAP_CAPTURE_CENTER){s->follow=1;s->follow_x_q16=s->follow_z_q16=0;follow(s,x,z);}
                if(old_zoom!=s->zoom&&s->follow){
                    s->follow_x_q16=s->center_x_q16-((int32_t)s->width-x)*65536;
                    s->follow_z_q16=s->center_z_q16-(int32_t)z*65536;
                }
                ++s->actions;
            }
        }else if(s->capture==DOK_MAP_CAPTURE_MAP){
            int32_t dx=t.x-s->last_x,dy=t.y-s->last_y;
            if(dx||dy){uint32_t scale=dok_map_scales[s->zoom];
                s->follow=0;s->center_x_q16-=dx*65536/(int32_t)scale;
                s->center_z_q16-=dy*65536/(int32_t)scale;++s->drags;
            }
            s->last_x=t.x;s->last_y=t.y;
        }
    }
    if(s->follow&&!t.paused)follow(s,x,z);
    bounds(s);
    if(memcmp(&old,s,sizeof(*s)))++s->revision;
}
void dok_map_control_view(const dok_map_control *s,int32_t *left,int32_t *top)
{
    uint32_t scale=dok_map_scales[s->zoom];
    *left=(int32_t)(((int64_t)s->center_x_q16*scale)/65536)-128;
    *top=(int32_t)(((int64_t)s->center_z_q16*scale)/65536)-80;
}
