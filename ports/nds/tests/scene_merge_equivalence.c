#include "dok/render_scene.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
extern int baseline_dok_render_merge(const dok_nds_render_quad *,const dok_nds_render_quad *,dok_nds_render_quad *);
static uint32_t state=UINT32_C(0xC921AE63),calls,accepted,rejected;
static uint32_t random32(void)
{state^=state<<13;state^=state>>17;state^=state<<5;return state;}
static int check(const dok_nds_render_quad *a,const dok_nds_render_quad *b,int required)
{
    dok_nds_render_quad old,newer,original;int sa,sb;uint32_t alias;
    for(alias=0;alias<3u;++alias) {
        if(alias==0u){memset(&old,0xa5,sizeof(old));newer=old;original=old;sa=baseline_dok_render_merge(a,b,&old);sb=dok_render_merge(a,b,&newer);}
        else if(alias==1u){old=*a;newer=*a;original=*a;sa=baseline_dok_render_merge(&old,b,&old);sb=dok_render_merge(&newer,b,&newer);}
        else {old=*b;newer=*b;original=*b;sa=baseline_dok_render_merge(a,&old,&old);sb=dok_render_merge(a,&newer,&newer);}
        if(sa!=sb || (required>=0 && sa!=required) || memcmp(&old,&newer,sizeof(old)) ||
            (!sa && memcmp(&newer,&original,sizeof(newer)))) {
            fprintf(stderr,"Merge mismatch call %u alias %u status %d/%d required %d\n",calls,alias,sa,sb,required);return 0;
        }
        ++calls;if(sb)++accepted;else++rejected;
    }
    return 1;
}
static void component_set(dok_nds_render_vertex *v,uint32_t axis,int32_t value)
{if(axis==0u)v->x=value;else if(axis==1u)v->y=value;else if(axis==2u)v->z=value;else if(axis==3u)v->u_q16=value;else v->v_q16=value;}
int main(void)
{
    uint32_t n,i,j;dok_nds_render_quad a,b,changed;
    for(n=0;n<20000u;++n) {
        uint32_t axis=n%3u,height_axis=(axis+1u)%3u;
        int32_t w=(int32_t)(random32()%4095u)+1,h=(int32_t)(random32()%8191u)+1;
        int32_t direction=(n&1u)?-1:1,hdirection=(n&2u)?-1:1;
        int32_t base=direction>0?-4096:4095,hbase=hdirection>0?-4096:4095;
        int32_t uvbase=(n&4u)?134213632:-134217728,du=(n&4u)?-134215680:134215680;
        memset(&a,0,sizeof(a));a.texture_slot=3;a.color_rgb555=32767;b=a;
        for(i=0;i<4u;++i) {
            int32_t x=(i==1u || i==2u)?w:0,y=i>=2u?h:0;
            component_set(&a.vertex[i],axis,base+direction*x);
            component_set(&a.vertex[i],height_axis,hbase+hdirection*y);
            a.vertex[i].u_q16=uvbase+(x?du:0);a.vertex[i].v_q16=(i>=2u)?134213632:-134217728;
            b.vertex[i]=a.vertex[i];
            component_set(&b.vertex[i],axis,base+direction*(x+w));
            b.vertex[i].u_q16=uvbase+du+(x?du:0);
        }
        if(!check(&a,&b,1))return 1;
        for(j=0;j<12u;++j) {
            changed=b;
            if(j==0u)++changed.texture_slot;
            else if(j==1u)++changed.flags;
            else if(j==2u)--changed.color_rgb555;
            else if(j==3u)++changed.reserved;
            else if(j==4u)++changed.vertex[1].u_q16;
            else if(j==5u)changed.vertex[1].x=4096;
            else if(j==6u)changed.vertex[1].y=-4097;
            else if(j==7u)changed.vertex[1].u_q16=134213633;
            else if(j==8u)changed.vertex[1].v_q16=-134217729;
            else if(j==9u)changed.vertex[1].z=INT32_MAX;
            else if(j==10u)changed.vertex[1].x=INT32_MIN;
            else ++changed.vertex[0].z;
            if(!check(&a,&changed,0))return 2;
        }
        if(!check(&a,&a,0))return 3;
    }
    for(n=0;n<100000u;++n) {
        memset(&a,0,sizeof(a));a.texture_slot=1;a.color_rgb555=31;b=a;
        for(i=0;i<4u;++i) {
            a.vertex[i]=(dok_nds_render_vertex){(int32_t)(random32()%8192u)-4096,
                (int32_t)(random32()%8192u)-4096,(int32_t)(random32()%8192u)-4096,
                (int32_t)(random32()%268431361u)-134217728,(int32_t)(random32()%268431361u)-134217728};
            b.vertex[i]=(dok_nds_render_vertex){(int32_t)(random32()%8192u)-4096,
                (int32_t)(random32()%8192u)-4096,(int32_t)(random32()%8192u)-4096,
                (int32_t)(random32()%268431361u)-134217728,(int32_t)(random32()%268431361u)-134217728};
        }
        b.vertex[0]=a.vertex[1];b.vertex[3]=a.vertex[2];
        if(!check(&a,&b,-1))return 4;
    }
    printf("{\"passed\":true,\"API_calls\":%u,\"accepted\":%u,\"rejected\":%u,"
        "\"positive_affine_pairs\":20000,\"attribute_boundary_mutations\":240000,"
        "\"random_shared_edge_pairs\":100000,\"alias_modes\":3}\n",calls,accepted,rejected);
    return 0;
}
