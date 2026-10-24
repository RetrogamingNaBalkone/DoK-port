#include "dok/animation.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 1; } } while (0)
int main(void) {
    /* Synthetic two-frame sequence; every serialized pointer contains garbage. */
    uint8_t bytes[71] = {2,0,0xff,0xff,0xff,0xff,0xff,0xff};
    uint8_t invalid[71];
    dok_animation_view a, unchanged;
    dok_animation_iterator it;
    dok_animation_frame frame;
    dok_image_view image;
    size_t i;
    bytes[8]=0xa0; bytes[9]=0x86; bytes[10]=0xfe; bytes[11]=0xff; bytes[12]=3;
    memset(bytes+14,0xee,4);
    bytes[18]=0xf4; bytes[19]=1; bytes[20]=1; bytes[22]=1;
    memset(bytes+24,0xee,4);
    bytes[28]=2; bytes[30]=2; bytes[32]=0x34; bytes[33]=0x12; bytes[34]=7; bytes[36]=3;
    bytes[38]=4; memset(bytes+42,0xdd,4);
    bytes[46]=0; bytes[47]=255; bytes[48]=128; bytes[49]=7;
    bytes[50]=1; bytes[52]=2; bytes[60]=2; memset(bytes+64,0xcc,4);
    bytes[68]=9; bytes[69]=0;
    CHECK(dok_animation_open(bytes,70,&a)==DOK_ASSET_OK && a.frame_count==2 && a.images_offset==28);
    CHECK(dok_animation_begin(&a,&it)==DOK_ASSET_OK);
    CHECK(dok_animation_next(&it,&frame)==DOK_ASSET_OK && frame.index==0 && frame.image_offset==28);
    CHECK(frame.interval_bits==0x86a0 && frame.interval_ticks==-31072 && frame.parameter_a==-2 && frame.parameter_b==3);
    CHECK(frame.image.width==2 && frame.image.height==2 && frame.image.stored_flags==0x1234);
    CHECK(frame.image.crop_divisor==7 && frame.image.crop_index==3 && frame.image.pixels==bytes+46);
    CHECK(frame.image.pixels[1]==255 && frame.image.pixels[2]==128);
    CHECK(dok_animation_next(&it,&frame)==DOK_ASSET_OK && frame.index==1 && frame.image_offset==50);
    CHECK(frame.interval_ticks==500 && frame.image.pixels[0]==9 && frame.image.pixels[1]==0);
    CHECK(dok_animation_next(&it,&frame)==DOK_ASSET_END && frame.index==1);
    CHECK(dok_animation_frame_at(&a,0,&frame)==DOK_ASSET_OK && frame.index==0);
    CHECK(dok_animation_frame_at(&a,2,&frame)==DOK_ASSET_END && frame.index==0);
    memcpy(&unchanged,&a,sizeof(a));
    for(i=0;i<70;++i) {
        CHECK(dok_animation_open(bytes,i,&a)!=DOK_ASSET_OK);
        CHECK(!memcmp(&a,&unchanged,sizeof(a)));
    }
    CHECK(dok_animation_open(bytes,71,&a)==DOK_ASSET_TRAILING_DATA);
    memcpy(invalid,bytes,sizeof(bytes)); invalid[0]=0;
    CHECK(dok_animation_open(invalid,70,&a)==DOK_ASSET_INVALID_FORMAT);
    invalid[1]=0x80;
    CHECK(dok_animation_open(invalid,70,&a)==DOK_ASSET_INVALID_FORMAT);
    memcpy(invalid,bytes,sizeof(bytes)); invalid[38]=3;
    CHECK(dok_animation_open(invalid,70,&a)==DOK_ASSET_INVALID_FORMAT);
    CHECK(dok_image_open(bytes+28,22,&image)==DOK_ASSET_OK);
    CHECK(dok_image_open(bytes+28,23,&image)==DOK_ASSET_TRAILING_DATA);
    CHECK(dok_image_read_prefix(bytes+28,42,&image)==DOK_ASSET_OK && image.encoded_size==22);
    memcpy(invalid,bytes,sizeof(bytes)); invalid[28]=0;
    CHECK(dok_image_open(invalid+28,22,&image)==DOK_ASSET_INVALID_FORMAT);
    CHECK(dok_image_open(NULL,0,&image)==DOK_ASSET_INVALID_ARGUMENT);
    CHECK(dok_animation_open(NULL,0,&a)==DOK_ASSET_INVALID_ARGUMENT);
    puts("animation/image: signed metadata, exact boundaries, iteration, truncation and pointer isolation passed");
    return 0;
}
