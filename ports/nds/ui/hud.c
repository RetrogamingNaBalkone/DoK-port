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
static void text(unsigned char *out,const unsigned char *bank,const unsigned char *s,uint32_t n,uint32_t x,uint32_t y,uint32_t h)
{
    const unsigned char *font=bank+(h==16u?2048u:0u);
    for(uint32_t c=0;c<n;++c)for(uint32_t r=0;r<h&&y+r<38u;++r){unsigned char bits=font[s[c]*h+r];
        for(uint32_t b=0;b<8u;++b)if(bits&(128u>>b))out[(y+r)*256u+x+c*8u+b]=2;
    }
}
static void icon(unsigned char *out,const unsigned char *pixels,uint32_t x,uint32_t y,uint32_t w,uint32_t h)
{for(uint32_t r=0;r<h;++r)for(uint32_t c=0;c<w;++c)if(pixels[r*w+c])out[(y+r)*256u+x+c]=pixels[r*w+c];}
typedef struct Message {unsigned char *out;const unsigned char *bank,*data;uint32_t y;} Message;
static void message_bounds(void *p,uint32_t offset,uint32_t col,uint32_t row)
{Message *m=p;(void)offset;(void)col;if(row)m->y=3u;}
static void message_glyph(void *p,uint32_t offset,uint32_t col,uint32_t row)
{Message *m=p;
 for(uint32_t y=0;y<8u;++y)for(uint32_t x=0;x<8u;++x)
    if(m->bank[m->data[offset]*8u+y]&(128u>>x))
        m->out[(m->y+row*8u+y)*256u+8u+col*8u+x]=2;
}
void dok_ui_hud(unsigned char out[DOK_UI_HUD_BYTES],const unsigned char bank[DOK_UI_BANK_BYTES],
    uint32_t language,const dok_ui_values *v,const unsigned char *message,uint32_t bytes,uint32_t page)
{
    const unsigned char *border=bank+7010u;unsigned char n[7];
    (void)language;(void)page;
    memset(out,86,DOK_UI_HUD_BYTES);
    memcpy(out,border,256);
    /* Original panel edge separates the full-height message from the stats. */
    memcpy(out+20u*256u,border,256);
    icon(out,bank+6144u,4,23,17,14);icon(out,bank+6382u,82,22,13,16);
    text(out,bank,n,dok_ui_number(v->health,n),23,23,16);
    text(out,bank,n,dok_ui_number(v->charge,n),97,23,16);
    for(uint32_t i=0;i<3u;++i)if(v->keys&(1u<<i))icon(out,bank+6590u+i*140u,212u+i*14u,23,10,14);
    /* weapon is the owned-slot mask, not the currently selected slot. */
    /* Each weapon fits its original aspect ratio in a 20x10 transparent tile. */
    for(uint32_t i=0;i<2u;++i)if(v->weapon&(1u<<(i+1u)))icon(out,bank+7778u+i*200u,150u+i*28u,25,20,10);
    Message m={out,bank,message,7u};
    (void)dok_ui_paginate(message,bytes,30,2,0,message_bounds,&m);
    (void)dok_ui_paginate(message,bytes,30,2,0,message_glyph,&m);
}
