#include "dok/vram_layout.h"
#include <stddef.h>
#include <string.h>

static const uint32_t bases[2]={0x06800000u,0x06860000u};
static const uint32_t limits[2]={0x06840000u,0x06880000u};
static int pool(uint32_t address,uint32_t bytes)
{
    for(unsigned n=0;n<2u;++n)
        if(address>=bases[n]&&address<limits[n]&&bytes<=limits[n]-address)return (int)n;
    return -1;
}
static int overlaps(uint32_t a,uint32_t bytes,uint32_t b,uint32_t size)
{return a<b+size&&b<a+bytes;}
typedef struct FreeSpan {uint32_t begin,end;} FreeSpan;

static int packable(const dok_vram_span *spans,uint32_t count,
    const uint32_t *missing,uint32_t nmissing,const unsigned char mask[16])
{
    uint16_t order[DOK_VRAM_LAYOUT_SLOTS];uint32_t sizes[DOK_VRAM_LAYOUT_SLOTS];
    FreeSpan free[DOK_VRAM_LAYOUT_SLOTS+2];uint32_t norder=0,nsizes=nmissing,nfree=0;
    memcpy(sizes,missing,nmissing*sizeof(*missing));
    for(uint32_t i=0;i<count;++i){
        if(mask[spans[i].slot/8u]&(1u<<(spans[i].slot%8u)))sizes[nsizes++]=spans[i].bytes;
        else order[norder++]=(uint16_t)i;
    }
    for(uint32_t i=1;i<norder;++i){uint16_t value=order[i];uint32_t j=i;
        while(j&&spans[order[j-1u]].address>spans[value].address){order[j]=order[j-1u];--j;}
        order[j]=value;
    }
    for(unsigned p=0;p<2u;++p){uint32_t cursor=bases[p];
        for(uint32_t i=0;i<norder;++i){const dok_vram_span *s=&spans[order[i]];
            if(pool(s->address,s->bytes)!=(int)p)continue;
            if(s->address>cursor)free[nfree++]=(FreeSpan){cursor,s->address};
            cursor=s->address+s->bytes;
        }
        if(cursor<limits[p])free[nfree++]=(FreeSpan){cursor,limits[p]};
    }
    for(uint32_t i=0;i<nsizes;++i){uint32_t largest=i;
        for(uint32_t j=i+1u;j<nsizes;++j)if(sizes[j]>sizes[largest])largest=j;
        uint32_t temp=sizes[i];sizes[i]=sizes[largest];sizes[largest]=temp;
        uint32_t j=0;
        for(;j<nfree;++j)if(sizes[i]<=free[j].end-free[j].begin){free[j].begin+=sizes[i];break;}
        if(j==nfree)return 0;
    }
    return 1;
}
static void consider(const dok_vram_span *spans,uint32_t count,
    const uint32_t *missing,uint32_t nmissing,uint32_t address,uint32_t required,
    dok_vram_relocation *best,int *found)
{
    if((address&7u)||pool(address,required)<0)return;
    dok_vram_relocation choice={address,required,0,0,{0}};
    for(uint32_t i=0;i<count;++i){const dok_vram_span *s=&spans[i];
        if(overlaps(address,required,s->address,s->bytes)){
            choice.evict[s->slot/8u]|=(unsigned char)(1u<<(s->slot%8u));
            choice.cost+=s->upload_cost;++choice.count;
        }
    }
    if(*found&&(choice.cost>best->cost||
       (choice.cost==best->cost&&choice.count>best->count)||
       (choice.cost==best->cost&&choice.count==best->count&&choice.address>=best->address)))return;
    if(!packable(spans,count,missing,nmissing,choice.evict))return;
    *best=choice;*found=1;
}
static void boundary(const dok_vram_span *spans,uint32_t count,
    const uint32_t *missing,uint32_t nmissing,uint32_t address,uint32_t required,
    dok_vram_relocation *best,int *found)
{
    consider(spans,count,missing,nmissing,address,required,best,found);
    if(address>=8u)consider(spans,count,missing,nmissing,address-8u,required,best,found);
    if(address<=UINT32_MAX-8u)consider(spans,count,missing,nmissing,address+8u,required,best,found);
}
int dok_vram_relocation_plan(const dok_vram_span *spans,uint32_t count,
    const uint32_t *missing,uint32_t nmissing,dok_vram_relocation *out)
{
    if(!out||!missing||!nmissing||count>DOK_VRAM_LAYOUT_SLOTS||
       nmissing>DOK_VRAM_LAYOUT_SLOTS-count||(count&&!spans))return 0;
    uint32_t total=0,required=0;unsigned char seen[16]={0};
    for(uint32_t i=0;i<count;++i){const dok_vram_span *s=&spans[i];
        if(!s->bytes||((s->address|s->bytes)&7u)||pool(s->address,s->bytes)<0||
           s->upload_cost>s->bytes||s->slot>=DOK_VRAM_LAYOUT_SLOTS||
           (seen[s->slot/8u]&(1u<<(s->slot%8u))))return 0;
        seen[s->slot/8u]|=(unsigned char)(1u<<(s->slot%8u));
        if(s->bytes>393216u-total)return 0;total+=s->bytes;
        for(uint32_t j=0;j<i;++j)
            if(overlaps(s->address,s->bytes,spans[j].address,spans[j].bytes))return 0;
    }
    for(uint32_t i=0;i<nmissing;++i){
        if(!missing[i]||(missing[i]&7u)||missing[i]>262144u||missing[i]>393216u-total)return 0;
        total+=missing[i];if(missing[i]>required)required=missing[i];
    }
    dok_vram_relocation best={0};int found=0;
    for(unsigned p=0;p<2u;++p){
        boundary(spans,count,missing,nmissing,bases[p],required,&best,&found);
        if(required<=limits[p]-bases[p])
            boundary(spans,count,missing,nmissing,limits[p]-required,required,&best,&found);
    }
    for(uint32_t i=0;i<count;++i){
        boundary(spans,count,missing,nmissing,spans[i].address+spans[i].bytes,required,&best,&found);
        if(spans[i].address>=required)
            boundary(spans,count,missing,nmissing,spans[i].address-required,required,&best,&found);
    }
    if(found)*out=best;return found;
}
