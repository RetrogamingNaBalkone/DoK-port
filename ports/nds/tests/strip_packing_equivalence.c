#define main ordinary_strip_tests
#include "strip_equality_equivalence.c"
#undef main
extern int old_strip_alias(const dok_nds_render_quad *,const unsigned char *,const dok_nds_render_quad *,unsigned char *);
extern int new_strip_alias(const dok_nds_render_quad *,const unsigned char *,const dok_nds_render_quad *,unsigned char *);
extern unsigned new_strip_decode(unsigned,const dok_nds_render_quad *,unsigned char *,unsigned char *);
int main(void)
{
    uint32_t alias_cases=0,decode_cases=0;
    if(ordinary_strip_tests())return 1;
    for(unsigned packed=0;packed<512;++packed){
        unsigned char ad[5],decoded[5],old_vertices[80]={0},new_vertices[80]={0};
        dok_nds_render_quad a={0},b={0};
        for(unsigned k=0;k<4;++k){ad[k]=(unsigned char)((packed>>(2u*k))&3u);a.vertex[k]=vertex();}
        ad[4]=(unsigned char)((packed>>8u)&1u);b=a;
        unsigned expected=0;
        for(unsigned k=ad[4]?2u:0u;k<4;++k){memcpy(old_vertices+20u*expected,&a.vertex[ad[k]],20);++expected;}
        unsigned actual=new_strip_decode(packed,&a,decoded,new_vertices);
        if(expected!=actual||memcmp(decoded,ad,5)||memcmp(old_vertices,new_vertices,sizeof(old_vertices)))return 2;
        ++decode_cases;
        for(unsigned i=0;i<4;++i)for(unsigned reverse=0;reverse<2;++reverse){
            unsigned j=(i+(reverse?3u:1u))%4u;
            for(unsigned variant=0;variant<4;++variant){
                unsigned char x[5],y[5];b=a;b.vertex[i]=a.vertex[3];b.vertex[j]=a.vertex[2];
                if(variant==1)b.color_rgb555^=1u;
                if(variant==2)b.texture_slot^=1u;
                if(variant==3){for(unsigned k=1;k<4;++k)a.vertex[k]=a.vertex[0];b=a;}
                int p=old_strip_alias(&a,ad,&b,x),q=new_strip_alias(&a,ad,&b,y);
                if(p!=q||memcmp(x,y,5))return 3;
                ++alias_cases;
            }
        }
    }
    printf("{\"passed\":true,\"helper_cases\":%"PRIu32",\"accepted\":%"PRIu32",\"alias_cases\":%"PRIu32",\"decode_states\":%"PRIu32"}\n",cases,accepted,alias_cases,decode_cases);
    return 0;
}
