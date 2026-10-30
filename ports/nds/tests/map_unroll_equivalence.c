#include "../ui/map_raster.c"
#include <inttypes.h>
#include <stdio.h>
extern int baseline_dok_map_raster(uint8_t *,const dok_map_snapshot *,unsigned,int32_t,int32_t);
extern void baseline_rect(uint8_t *,int32_t,int32_t,int32_t,int32_t,uint8_t);
#define GUARD 40u
_Alignas(4) static uint8_t old_buffer[DOK_MAP_VIEW_BYTES+2u*GUARD+4u],new_buffer[DOK_MAP_VIEW_BYTES+2u*GUARD+4u];
static uint8_t planes[3][4096],visited[4096];
static dok_map_glyph glyphs[256];
static uint32_t state=UINT32_C(0xC12A91F3),raster_cases,rect_cases,guard_cases;
static uint64_t pixels;
static uint32_t random32(void)
{state^=state<<13;state^=state>>17;state^=state<<5;return state;}
static int buffer_equal(uint32_t alignment)
{
    uint32_t i,start=GUARD+alignment,end=start+DOK_MAP_VIEW_BYTES;
    if(memcmp(old_buffer,new_buffer,sizeof(old_buffer)))return 0;
    for(i=0;i<sizeof(new_buffer);++i)if((i<start || i>=end) && new_buffer[i]!=0xa5u)return 0;
    pixels+=DOK_MAP_VIEW_BYTES;return 1;
}
static int raster(const dok_map_snapshot *s,unsigned scale,int32_t x,int32_t z,uint32_t alignment)
{
    int a,b;memset(old_buffer,0xa5,sizeof(old_buffer));memset(new_buffer,0xa5,sizeof(new_buffer));
    a=baseline_dok_map_raster(old_buffer+GUARD+alignment,s,scale,x,z);
    b=dok_map_raster(new_buffer+GUARD+alignment,s,scale,x,z);
    if(a!=b || !buffer_equal(alignment))return 0;++raster_cases;return 1;
}
static int rectangle(int32_t x1,int32_t y1,int32_t x2,int32_t y2,uint32_t alignment)
{
    uint8_t colour=(uint8_t)random32();memset(old_buffer,0xa5,sizeof(old_buffer));memset(new_buffer,0xa5,sizeof(new_buffer));
    baseline_rect(old_buffer+GUARD+alignment,x1,y1,x2,y2,colour);
    rect(new_buffer+GUARD+alignment,x1,y1,x2,y2,colour);
    if(!buffer_equal(alignment))return 0;++rect_cases;return 1;
}
int main(void)
{
    uint32_t n,i,j,k,a;dok_map_snapshot s={0};
    static const unsigned scales[]={2,3,4,6,8};
    static const int32_t starts[]={-20,-9,-1,0,1,247,254,255,256};
    static const int32_t ys[]={-20,-9,-1,0,1,151,158,159,160};
    static const int32_t offsets[]={-32768,-513,-256,-9,-1,0,1,159,255,256,32767};
    s.maps[0]=planes[0];s.maps[1]=planes[1];s.maps[2]=planes[2];s.visited=visited;s.glyphs=glyphs;
    for(n=0;n<512u;++n) {
        s.width=(uint16_t)(random32()%64u+1u);s.length=(uint16_t)(random32()%64u+1u);
        s.hero_x=(int16_t)((int32_t)(random32()%(s.width+2u))-1);s.hero_z=(int16_t)((int32_t)(random32()%(s.length+2u))-1);
        s.heading=(uint16_t)(random32()%65536u);s.map_available=(uint8_t)(random32()&1u);
        for(i=0;i<256u;++i)glyphs[i]=(dok_map_glyph){(uint16_t)(random32()%16u),(uint8_t)random32(),(uint8_t)(random32()%6u)};
        for(i=0;i<(uint32_t)s.width*s.length;++i) {
            for(j=0;j<3u;++j)planes[j][i]=(uint8_t)random32();visited[i]=(uint8_t)(random32()&1u);
        }
        for(k=0;k<5u;++k)for(a=0;a<4u;++a) {
            int32_t x=n%5u?(int32_t)(random32()%769u)-512:offsets[n%11u];
            int32_t z=n%5u?(int32_t)(random32()%673u)-512:offsets[(n+3u)%11u];
            if(!raster(&s,scales[k],x,z,a))return 1;
        }
    }
    s.width=4;s.length=4;s.hero_x=1;s.hero_z=1;s.map_available=1;
    memset(planes,0,sizeof(planes));memset(visited,1,sizeof(visited));memset(glyphs,0,sizeof(glyphs));
    planes[0][5]=1;glyphs[1]=(dok_map_glyph){1,42,0};
    for(n=0;n<=361u;++n){s.heading=(uint16_t)n;for(k=0;k<5u;++k)for(a=0;a<4u;++a)
        if(!raster(&s,scales[k],-7,9,a))return 2;}
    for(n=1;n<=12u;++n)for(i=0;i<9u;++i)for(j=0;j<9u;++j)for(a=0;a<4u;++a) {
        if(!rectangle(starts[i],ys[j],starts[i]+(int32_t)n-1,ys[j]+(int32_t)((i+j)%10u),a))return 3;
        if(!rectangle(starts[i]+(int32_t)n-1,ys[j]+(int32_t)((i+j)%10u),starts[i],ys[j],a))return 4;
    }
    for(a=0;a<4u;++a) {
        if(!rectangle(INT32_MIN,INT32_MIN,INT32_MAX,INT32_MAX,a))return 5;
        if(!rectangle(INT32_MAX,INT32_MAX,INT32_MIN,INT32_MIN,a))return 6;
        for(n=0;n<8u;++n) {
            unsigned scale=n<6u?(unsigned[]){0,1,5,7,9,UINT32_MAX}[n]:3u;
            int32_t x=n==6u?-32769:0,z=n==7u?32768:0;
            if(!raster(&s,scale,x,z,a))return 7;++guard_cases;
        }
        {dok_map_snapshot invalid=s;invalid.width=0;if(!raster(&invalid,3,0,0,a))return 8;++guard_cases;
         invalid=s;invalid.length=65;if(!raster(&invalid,3,0,0,a))return 9;++guard_cases;
         invalid=s;invalid.maps[1]=NULL;if(!raster(&invalid,3,0,0,a))return 10;++guard_cases;}
    }
    printf("{\"passed\":true,\"raster_cases\":%u,\"rect_cases\":%u,\"guard_cases\":%u,"
        "\"alignments\":4,\"pixels_compared\":%" PRIu64 "}\n",raster_cases,rect_cases,guard_cases,pixels);
    return 0;
}
