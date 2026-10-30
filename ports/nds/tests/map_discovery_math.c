/* Exercise the actual private production helpers, not a reimplementation. */
#include "../ui/map_discovery.c"
#include <inttypes.h>
#include <stdio.h>

static uint32_t random_state=UINT32_C(0x21D15C32);
static uint32_t random_bits(void)
{random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state;}
static int32_t original_wrap(int64_t n)
{uint32_t u=(uint32_t)(uint64_t)n;return u<=INT32_MAX?(int32_t)u:-1-(int32_t)(UINT32_MAX-u);}
static int32_t original_sar(int32_t n,uint32_t b)
{int64_t d=INT64_C(1)<<b;return n>=0?(int32_t)(n/d):-(int32_t)((-(int64_t)n+d-1)/d);}
static int32_t original_rotate(const dok_nds_render_camera *c,int32_t x,int32_t z,uint32_t depth)
{
    int32_t dx=original_wrap((int64_t)x-c->x),dz=original_wrap((int64_t)z-c->z);
    return depth?original_wrap((int64_t)dz*c->cos_q15-(int64_t)dx*c->sin_q15)
        :original_wrap((int64_t)dx*c->cos_q15+(int64_t)dz*c->sin_q15);
}

int main(void)
{
    uint64_t shifts=0,rotations=0;uint32_t i,j,k,l,m,n,b;
    static const uint32_t used_shifts[]={1,2,15,20,31};
    static const int32_t edges[]={INT32_MIN,INT32_MIN+1,-65536,-1,0,1,65535,INT32_MAX};
    static const int32_t trig_edges[]={-32768,-1,0,1,32768};
    dok_nds_render_camera camera={0};
    for(i=0;i<131072u;++i) {
        int32_t value=(int32_t)i-65536;
        for(j=0;j<sizeof(used_shifts)/sizeof(used_shifts[0]);++j) {
            b=used_shifts[j];if(sar(value,b)!=original_sar(value,b))return 1;++shifts;
        }
    }
    for(i=0;i<200000u;++i) {
        int32_t value=original_wrap(random_bits());
        for(b=1;b<32u;++b){if(sar(value,b)!=original_sar(value,b))return 2;++shifts;}
        camera.x=original_wrap(random_bits());camera.z=original_wrap(random_bits());
        camera.sin_q15=(int32_t)(random_bits()%65537u)-32768;
        camera.cos_q15=(int32_t)(random_bits()%65537u)-32768;
        {
            int32_t x=original_wrap(random_bits()),z=original_wrap(random_bits());
            for(j=0;j<2u;++j){if(rotate(&camera,x,z,j)!=original_rotate(&camera,x,z,j))return 3;++rotations;}
        }
    }
    for(i=0;i<8u;++i) {
        for(b=1;b<32u;++b){if(sar(edges[i],b)!=original_sar(edges[i],b))return 4;++shifts;}
        for(j=0;j<8u;++j)for(k=0;k<8u;++k)for(l=0;l<8u;++l)
            for(m=0;m<5u;++m)for(n=0;n<5u;++n) {
                camera.x=edges[k];camera.z=edges[l];camera.sin_q15=trig_edges[m];camera.cos_q15=trig_edges[n];
                for(b=0;b<2u;++b) {
                    if(rotate(&camera,edges[i],edges[j],b)!=original_rotate(&camera,edges[i],edges[j],b))return 5;
                    ++rotations;
                }
            }
    }
    printf("{\"passed\":true,\"actual_C_sar_vectors\":%" PRIu64 ","
        "\"actual_C_rotation_vectors\":%" PRIu64 ",\"shift_min\":1,\"shift_max\":31}\n",shifts,rotations);
    return 0;
}
