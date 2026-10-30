#include "dok/map_control.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static unsigned checks;
#define CHECK(e) do {++checks;if(!(e)){fprintf(stderr,"line %u: %s\n",__LINE__,#e);abort();}} while(0)
static void tap(dok_map_control *s,int x,int y)
{dok_map_control_step(s,(dok_map_touch){0,0,0,0},32,32);dok_map_control_step(s,(dok_map_touch){(int16_t)x,(int16_t)y,1,0},32,32);}
static int32_t anchor(int32_t old,unsigned cells,unsigned pixels,unsigned scale)
{
    if(cells*scale<=pixels)return (int32_t)cells*32768;
    int32_t edge=(int32_t)((pixels*32768u+scale-1u)/scale);
    return old<edge?edge:old>(int32_t)cells*65536-edge?(int32_t)cells*65536-edge:old;
}
int main(void)
{
    dok_map_control s;int32_t left,top;
    for(unsigned w=1;w<=64;++w)for(unsigned h=1;h<=64;++h){
        dok_map_control_init(&s,(uint16_t)w,(uint16_t)h,0,0);
        CHECK(s.follow&&s.zoom==1);
        for(unsigned z=0;z<5;++z){
            s.zoom=(uint8_t)z;s.follow=0;
            for(int edge=0;edge<4;++edge){
                s.center_x_q16=edge&1?INT32_C(64000000):-64000000;
                s.center_z_q16=edge&2?INT32_C(64000000):-64000000;
                dok_map_control_step(&s,(dok_map_touch){0,0,0,0},0,0);
                dok_map_control_view(&s,&left,&top);int scale=dok_map_scales[z];
                CHECK(w*(unsigned)scale<=256u?left==((int)(w*(unsigned)scale)/2)-128:left>=0&&left<=(int)w*scale-256);
                CHECK(h*(unsigned)scale<=160u?top==((int)(h*(unsigned)scale)/2)-80:top>=0&&top<=(int)h*scale-160);
            }
        }
    }
    dok_map_control_init(&s,64,64,32,32);
    tap(&s,100,170);CHECK(s.zoom==2&&s.actions==1&&s.capture==3);
    dok_map_control saved=s;
    for(int i=0;i<99;++i)dok_map_control_step(&s,(dok_map_touch){0,0,1,0},32,32);
    CHECK(s.zoom==saved.zoom&&s.center_x_q16==saved.center_x_q16&&s.follow);
    tap(&s,100,170);CHECK(s.zoom==3);
    tap(&s,100,100);saved=s;
    CHECK(s.center_x_q16==saved.center_x_q16);
    dok_map_control_step(&s,(dok_map_touch){106,112,1,0},32,32);
    CHECK(!s.follow&&s.center_x_q16==saved.center_x_q16-65536&&s.center_z_q16==saved.center_z_q16-131072);
    saved=s;tap(&s,250,140);CHECK(s.center_x_q16==saved.center_x_q16&&s.center_z_q16==saved.center_z_q16);
    dok_map_control_step(&s,(dok_map_touch){240,140,1,1},32,32);CHECK(!s.capture&&s.blocked);
    saved=s;dok_map_control_step(&s,(dok_map_touch){0,0,1,0},32,32);
    CHECK(s.center_x_q16==saved.center_x_q16&&s.center_z_q16==saved.center_z_q16);
    tap(&s,200,175);CHECK(s.follow&&s.capture==4);
    for(int i=0;i<20;++i)tap(&s,100,175);
    CHECK(s.zoom==4);for(int i=0;i<20;++i)tap(&s,20,175);CHECK(s.zoom==0);
    /* Zoom anchor at the center of a large map, away from all clamps. */
    s.follow=0;s.zoom=2;s.center_x_q16=32*65536;s.center_z_q16=32*65536;
    saved=s;tap(&s,100,175);CHECK(s.center_x_q16==saved.center_x_q16&&s.center_z_q16==saved.center_z_q16);
    /* Anchor is independent of follow mode, including old-scale clamps. */
    static const uint16_t sizes[4]={1,17,32,64};
    for(unsigned wi=0;wi<4;++wi)for(unsigned hi=0;hi<4;++hi)
    for(unsigned zoom=0;zoom<5;++zoom)for(unsigned mode=0;mode<2;++mode)
    for(unsigned where=0;where<5;++where)for(unsigned plus=0;plus<2;++plus){
        int16_t hx=(int16_t)(where==4?sizes[wi]/2u:(where&1u?sizes[wi]-1u:0u));
        int16_t hz=(int16_t)(where==4?sizes[hi]/2u:(where&2u?sizes[hi]-1u:0u));
        dok_map_control_init(&s,sizes[wi],sizes[hi],hx,hz);s.zoom=(uint8_t)zoom;
        dok_map_control_step(&s,(dok_map_touch){0,0,0,0},hx,hz);s.follow=(uint8_t)mode;
        saved=s;unsigned next=plus?(zoom<4u?zoom+1u:4u):(zoom?zoom-1u:0u);
        dok_map_control_step(&s,(dok_map_touch){(int16_t)(plus?120:20),175,1,0},hx,hz);
        CHECK(s.center_x_q16==anchor(saved.center_x_q16,s.width,256,dok_map_scales[next]));
        CHECK(s.center_z_q16==anchor(saved.center_z_q16,s.length,160,dok_map_scales[next]));
        CHECK(s.follow==mode&&s.zoom==next);
        saved=s;dok_map_control_step(&s,(dok_map_touch){0,0,0,0},hx,hz);
        CHECK(s.center_x_q16==saved.center_x_q16&&s.center_z_q16==saved.center_z_q16);
    }
    dok_map_control_init(&s,64,64,32,63);s.zoom=0;
    dok_map_control_step(&s,(dok_map_touch){0,0,0,0},32,63);
    dok_map_control_step(&s,(dok_map_touch){120,175,1,0},32,63);
    CHECK(s.follow&&s.center_z_q16==32*65536&&s.follow_z_q16==-31*65536);
    dok_map_control_step(&s,(dok_map_touch){0,0,0,0},32,62);
    CHECK(s.center_z_q16==31*65536);
    dok_map_control_step(&s,(dok_map_touch){210,175,1,0},32,62);
    CHECK(s.follow_z_q16==0&&s.center_z_q16==anchor(62*65536,64,160,3));
    /* Real DS samples can become invalid at the digitizer boundary. Neither
     * map nor captured button may turn a held re-entry into another action. */
    static const int16_t outside[4][2]={{-1,70},{256,70},{120,-1},{120,192}};
    for(unsigned edge=0;edge<4;++edge)for(unsigned button=0;button<2;++button){
        dok_map_control_init(&s,64,64,32,32);s.zoom=4;
        dok_map_control_step(&s,(dok_map_touch){120,(int16_t)(button?175:70),1,0},32,32);
        CHECK(s.capture==(button?3:1));saved=s;
        dok_map_control_step(&s,(dok_map_touch){outside[edge][0],outside[edge][1],1,0},32,32);
        CHECK(s.blocked&&s.held&&!s.capture&&s.center_x_q16==saved.center_x_q16&&s.center_z_q16==saved.center_z_q16);
        dok_map_control_step(&s,(dok_map_touch){120,70,1,0},32,32);
        CHECK(s.blocked&&!s.capture&&s.center_x_q16==saved.center_x_q16&&s.center_z_q16==saved.center_z_q16&&s.actions==saved.actions);
        dok_map_control_step(&s,(dok_map_touch){0,0,0,0},32,32);
        CHECK(!s.blocked&&!s.held&&!s.capture);
        dok_map_control_step(&s,(dok_map_touch){220,140,1,0},32,32);
        CHECK(s.capture==1&&!s.blocked&&s.center_x_q16==saved.center_x_q16&&s.center_z_q16==saved.center_z_q16&&s.actions==saved.actions);
    }
    puts("{\"task\":\"N20\",\"passed\":true}");printf("checks=%u\n",checks);return 0;
}
