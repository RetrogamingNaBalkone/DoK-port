#include "render_backend.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
extern int old_strip(const dok_nds_render_quad *,const unsigned char *,const dok_nds_render_quad *,unsigned char *);
extern int new_strip(const dok_nds_render_quad *,const unsigned char *,const dok_nds_render_quad *,unsigned char *);
static uint32_t rng=UINT32_C(0x12051288),cases,accepted;
static uint32_t bits(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static int32_t number(void){uint32_t u=bits();int32_t v;memcpy(&v,&u,4);return v;}
static dok_nds_render_vertex vertex(void)
{return (dok_nds_render_vertex){number(),number(),number(),number(),number()};}
static int32_t *component(dok_nds_render_vertex *v,unsigned n)
{switch(n){case 0:return &v->x;case 1:return &v->y;case 2:return &v->z;case 3:return &v->u_q16;default:return &v->v_q16;}}
static void flip(dok_nds_render_vertex *v,unsigned n)
{uint32_t u;int32_t *p=component(v,n);memcpy(&u,p,4);u^=1u;memcpy(p,&u,4);}
static int compare(const dok_nds_render_quad *a,const unsigned char *ad,const dok_nds_render_quad *b)
{
    unsigned char x[21],y[21];dok_nds_render_quad saved_a,saved_b=*b;
    unsigned char saved_ad[5];
    if(a)saved_a=*a;if(ad)memcpy(saved_ad,ad,5);
    memset(x,0xa5,sizeof(x));memset(y,0xa5,sizeof(y));
    int p=old_strip(a,ad,b,x+8),q=new_strip(a,ad,b,y+8);
    if(p!=q||memcmp(x,y,sizeof(x))||memcmp(b,&saved_b,sizeof(*b))||
       (a&&memcmp(a,&saved_a,sizeof(*a)))||(ad&&memcmp(ad,saved_ad,5))){
        fprintf(stderr,"strip mismatch %"PRIu32"\n",cases);return 0;
    }
    for(unsigned i=0;i<8;++i)if(x[i]!=0xa5||x[i+13]!=0xa5)return 0;
    ++cases;accepted+=(uint32_t)p;return 1;
}
static void permutation(unsigned rank,unsigned char p[5])
{
    unsigned mask=15;
    for(unsigned n=0;n<4;++n){unsigned factorial=n==0?6:n==1?2:1,k=rank/factorial;rank%=factorial;
        for(unsigned j=0;j<4;++j)if(mask&(1u<<j)){if(!k){p[n]=(unsigned char)j;mask&=~(1u<<j);break;}--k;}}
    p[4]=(unsigned char)(bits()&1u);
}
int main(void)
{
    static const int32_t edges[]={INT32_MIN,INT32_MIN+1,-65536,-1,0,1,65535,INT32_MAX};
    for(unsigned n=0;n<8192;++n){
        dok_nds_render_quad a={0},b={0};unsigned char ad[5];permutation(n%24,ad);
        for(unsigned k=0;k<4;++k){a.vertex[k]=vertex();b.vertex[k]=vertex();}
        if(n<8)for(unsigned k=0;k<4;++k)for(unsigned c=0;c<5;++c)*component(&a.vertex[k],c)=edges[(n+k+c)%8];
        a.texture_slot=b.texture_slot=(uint16_t)bits();a.color_rgb555=b.color_rgb555=(uint16_t)bits();
        a.flags=(uint16_t)bits();b.flags=(uint16_t)bits();a.reserved=(uint16_t)bits();b.reserved=(uint16_t)bits();
        if(!compare(&a,ad,&b)||!compare(NULL,NULL,&b))return 1;
        for(unsigned i=0;i<4;++i)for(unsigned reverse=0;reverse<2;++reverse){
            unsigned j=(i+(reverse?3u:1u))%4u;dok_nds_render_quad joined=b;
            joined.vertex[i]=a.vertex[ad[2]];joined.vertex[j]=a.vertex[ad[3]];
            if(!compare(&a,ad,&joined))return 1;
            for(unsigned c=0;c<10;++c){dok_nds_render_quad mismatch=joined;flip(&mismatch.vertex[c<5?i:j],c%5);if(!compare(&a,ad,&mismatch))return 1;}
            joined.texture_slot^=1u;if(!compare(&a,ad,&joined))return 1;
        }
        b=a;b.color_rgb555^=1u;if(!compare(&a,ad,&b))return 1;
        for(unsigned k=1;k<4;++k)a.vertex[k]=a.vertex[0];b=a;
        if(!compare(&a,ad,&b))return 1;
    }
    printf("{\"passed\":true,\"cases\":%"PRIu32",\"accepted\":%"PRIu32"}\n",cases,accepted);
    return 0;
}
