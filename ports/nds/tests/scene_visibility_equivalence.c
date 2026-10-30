/* Test the actual new private helper; the archived old helper is exported by
 * an appended host-only wrapper in the independently compiled baseline TU. */
#include "../render/scene.c"
#include <inttypes.h>
#include <stdio.h>
extern int baseline_visible(const dok_geometry_quad *,const dok_nds_render_camera *);
static uint32_t state=UINT32_C(0xC82151B1),cases;
static uint32_t random_bits(void)
{state^=state<<13;state^=state>>17;state^=state<<5;return state;}
static int compare(const dok_geometry_quad *q,const dok_nds_render_camera *camera)
{
    scene_camera_dot dot;
    dot.lateral=(int64_t)camera->x*camera->cos_q15+(int64_t)camera->z*camera->sin_q15;
    dot.depth=(int64_t)camera->z*camera->cos_q15-(int64_t)camera->x*camera->sin_q15;
    if(visible(q,camera,&dot)!=baseline_visible(q,camera)) {
        fprintf(stderr,"Visibility mismatch %u\n",cases);return 0;
    }
    ++cases;return 1;
}
int main(void)
{
    uint32_t n,i,j,k,l,m,p;dok_geometry_quad q={0};dok_nds_render_camera camera={0};
    static const int32_t edges[]={INT32_MIN,INT32_MIN+1,-65536,-1,0,1,65535,INT32_MAX};
    static const int32_t trig[]={-32768,-1,0,1,32768};
    static const int32_t distances[]={-10000,-1,0,1,10000};
    for(n=0;n<500000u;++n) {
        camera.x=wrap32(random_bits());camera.y=wrap32(random_bits());camera.z=wrap32(random_bits());
        camera.sin_q15=(int32_t)(random_bits()%65537u)-32768;
        camera.cos_q15=(int32_t)(random_bits()%65537u)-32768;
        for(i=0;i<4u;++i) {
            q.vertices[i].x=wrap32(random_bits());q.vertices[i].y=wrap32(random_bits());q.vertices[i].z=wrap32(random_bits());
        }
        if(!compare(&q,&camera))return 1;
    }
    for(n=0;n<8u;++n)for(i=0;i<8u;++i)for(j=0;j<8u;++j)for(k=0;k<8u;++k)
        for(l=0;l<5u;++l)for(m=0;m<5u;++m) {
            camera=(dok_nds_render_camera){edges[i],edges[j],edges[k],trig[l],trig[m]};
            for(p=0;p<4u;++p)q.vertices[p]=(dok_geometry_vertex){edges[(n+p)&7u],edges[(n+3u*p)&7u],edges[(n+5u*p)&7u],0,0};
            if(!compare(&q,&camera))return 2;
        }
    camera=(dok_nds_render_camera){0,0,0,0,32768};
    for(n=0;n<5u;++n)for(i=0;i<5u;++i)for(j=0;j<81u;++j) {
        uint32_t choices=j;int32_t t=distances[i];
        for(p=0;p<4u;++p) {
            int32_t delta=(int32_t)(choices%3u)-1;choices/=3u;
            int32_t x=0,y=0,z=0;
            if(n==0u)z=-1+delta;
            else if(n==1u){x=-81*t+delta;z=100*t-200;}
            else if(n==2u){x=163*t+delta;z=200*t-200;}
            else if(n==3u){y=-41*t+delta;z=100*t-200;}
            else{y=83*t+delta;z=200*t-200;}
            q.vertices[p]=(dok_geometry_vertex){x,y,z,0,0};
        }
        if(!compare(&q,&camera))return 3;
    }
    printf("{\"passed\":true,\"numeric_quads\":%u,\"random_full_signed32_quads\":500000,"
        "\"signed32_extreme_quads\":102400,\"exact_plane_boundary_quads\":2025}\n",cases);
    return 0;
}
