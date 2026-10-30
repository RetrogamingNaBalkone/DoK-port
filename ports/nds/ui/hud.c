#include "dok/ui.h"
#include <string.h>
uint32_t dok_ui_number(int16_t value,unsigned char out[7])
{
    unsigned char reverse[6];uint32_t n=0,p=0;int32_t v=value;
    if(v<0){out[p++]='-';v=-v;}
    do{reverse[n++]=(unsigned char)('0'+v%10);v/=10;}while(v);
    while(n)out[p++]=reverse[--n];out[p]=0;return p;
}
uint32_t dok_ui_paginate(const unsigned char *data,uint32_t bytes,uint32_t cols,
    uint32_t rows,uint32_t wanted,dok_ui_glyph_fn glyph,void *context)
{
    uint32_t stop=0,offset=0,page=0;
    if(!data||!cols||!rows)return 0;
    while(stop<bytes&&data[stop]&&data[stop]!=36u)++stop;
    do{uint32_t row=0,col=0;
        while(offset<stop){unsigned char c=data[offset];
            if(c==13u){++offset;continue;}
            if(c==10u){++offset;++row;col=0;if(row==rows)break;continue;}
            if(col==cols){col=0;++row;}
            if(c!=32u&&col&&(!offset||data[offset-1u]==10u||data[offset-1u]==13u||data[offset-1u]==32u)){
                uint32_t end=offset;while(end<stop&&data[end]!=10u&&data[end]!=13u&&data[end]!=32u)++end;
                if(end-offset<=cols&&col+end-offset>cols){col=0;++row;}
            }
            if(row==rows)break;
            if(page==wanted&&glyph)glyph(context,offset,col,row);
            ++offset;++col;
        }
        ++page;
    }while(offset<stop);
    return page;
}
static void fill(unsigned char *out,uint32_t x,uint32_t y,uint32_t w,uint32_t h,unsigned char c)
{for(uint32_t r=0;r<h;++r)memset(out+(y+r)*256u+x,c,w);}
static void text(unsigned char *out,const unsigned char *bank,const unsigned char *s,uint32_t n,uint32_t x,uint32_t y,uint32_t h)
{
    const unsigned char *font=bank+(h==16u?2048u:0u);
    for(uint32_t c=0;c<n;++c)for(uint32_t r=0;r<h;++r){unsigned char bits=font[s[c]*h+r];
        for(uint32_t b=0;b<8u;++b)if(bits&(128u>>b))out[(y+r)*256u+x+c*8u+b]=2;
    }
}
static void icon(unsigned char *out,const unsigned char *pixels,uint32_t x,uint32_t y,uint32_t w,uint32_t h)
{for(uint32_t r=0;r<h;++r)for(uint32_t c=0;c<w;++c)if(pixels[r*w+c])out[(y+r)*256u+x+c]=pixels[r*w+c];}
typedef struct Message {unsigned char *out;const unsigned char *bank,*data;} Message;
static void message_glyph(void *p,uint32_t offset,uint32_t col,uint32_t row)
{Message *m=p;(void)row;text(m->out,m->bank,m->data+offset,1,4u+col*8u,28,8);}
void dok_ui_hud(unsigned char out[DOK_UI_HUD_BYTES],const unsigned char bank[DOK_UI_BANK_BYTES],
    uint32_t language,const dok_ui_values *v,const unsigned char *message,uint32_t bytes,uint32_t page)
{
    const unsigned char *border=bank+7010u;unsigned char n[7],line[24];uint32_t count=0;
    memset(out,86,DOK_UI_HUD_BYTES);
    memcpy(out,border,256);memcpy(out+36u*256u,border+256,512);
    for(uint32_t r=1;r<36u;++r){memcpy(out+r*256u,border+768u+(r-1u)*6u,3);memcpy(out+r*256u+253u,border+771u+(r-1u)*6u,3);}
    icon(out,bank+6144u,4,3,17,14);icon(out,bank+6382u,82,2,13,16);
    text(out,bank,n,dok_ui_number(v->health,n),23,2,16);
    text(out,bank,n,dok_ui_number(v->charge,n),97,2,16);
    int32_t values[2]={v->health,v->charge},maxima[2]={v->max_health,v->max_charge};
    for(uint32_t i=0;i<2u;++i){uint32_t extent=0,x=i?97u:23u;fill(out,x,18,48,1,0);
        if(maxima[i]>0&&values[i]>0){extent=(uint32_t)values[i]*48u/(uint32_t)maxima[i];if(extent>48u)extent=48u;}
        if(extent)fill(out,x,18,extent,1,2);
    }
    for(uint32_t i=0;i<3u;++i)if(v->keys&(1u<<i))icon(out,bank+6590u+i*140u,156u+i*16u,3,10,14);
    line[0]=language==1u?0x8eu:'W';line[1]=':';line[2]=(unsigned char)('0'+(v->weapon<=8u?v->weapon:0u));text(out,bank,line,3,220,2,16);
    line[count++]=language==1u?0x8fu:'L';line[count++]=':';count+=dok_ui_number(v->lives,line+count);
    line[count++]=' ';line[count++]=' ';line[count++]=language==1u?0x91u:'S';line[count++]=':';count+=dok_ui_number(v->score,line+count);
    text(out,bank,line,count,4,20,8);
    Message m={out,bank,message};uint32_t pages=dok_ui_paginate(message,bytes,28,1,page,message_glyph,&m);
    if(pages>1u){line[0]=(unsigned char)('1'+page);line[1]='/';line[2]=(unsigned char)('0'+pages);text(out,bank,line,3,228,28,8);}
}
