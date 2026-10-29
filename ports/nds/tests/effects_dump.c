#include "dok/effects.h"
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
static unsigned char source[65536],output[65536],light[4096],rgb6[768];
static uint16_t palette[4096];
static int bytes(unsigned char *p,uint32_t n)
{ uint32_t i;unsigned value;for(i=0;i<n;++i) {if(scanf("%2x",&value)!=1)return 0;p[i]=(unsigned char)value;}return 1; }
int main(int argc,char **argv)
{
    if(argc!=2)return 1;
    if(!strcmp(argv[1],"--light")) {
        int32_t depth,near_depth;
        while(scanf("%"SCNd32" %"SCNd32,&depth,&near_depth)==2)
            printf("%lu\n",(unsigned long)dok_light_row(depth,(int16_t)near_depth));
    } else if(!strcmp(argv[1],"--pixels")) {
        unsigned w,h,sw,sh,flags,divisor,phase,masked;uint32_t i,n;
        while(scanf("%u %u %u %u %u %u %u %u",&w,&h,&sw,&sh,&flags,&divisor,&phase,&masked)==8) {
            n=sw*sh;if(n>sizeof(source) || !bytes(source,n) || !bytes(light,256))return 2;
            if(dok_effects_pixels(source,(uint16_t)w,(uint16_t)h,(uint16_t)sw,(uint16_t)sh,
                (uint16_t)flags,(uint16_t)divisor,(uint16_t)phase,light,masked,output,sizeof(output))!=DOK_STATUS_OK)return 3;
            for(i=0;i<n;++i)printf("%02x",output[i]);putchar('\n');
        }
    } else if(!strcmp(argv[1],"--palette")) {
        uint32_t percentage,i;
        while(scanf("%"SCNu32,&percentage)==1) {
            if(!bytes(rgb6,768) || !bytes(light,4096))return 4;
            if(dok_light_palette(rgb6,light,percentage,palette)!=DOK_STATUS_OK)return 5;
            for(i=0;i<4096u;++i)printf("%04x",palette[i]);putchar('\n');
        }
    } else return 6;
    return 0;
}
