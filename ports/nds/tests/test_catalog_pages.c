#include "../platform/assets/file_reader.h"
#include <stdio.h>
#include <string.h>
static unsigned char guarded[32768+64],expected[128],actual[128];
int main(int argc,char **argv)
{
    if(argc!=2)return 1;
    NdsAssetFile f;dok_asset_reader r;FILE *source=fopen(argv[1],"rb");unsigned char h[160];
    if(!source||fread(h,1,160,source)!=160||nds_asset_file_open(&f,argv[1],&r)!=DOK_ASSET_OK)return 2;
    uint32_t count=h[16]|(uint32_t)h[17]<<8|(uint32_t)h[18]<<16|(uint32_t)h[19]<<24;
    memset(guarded,0xa5,sizeof(guarded));nds_asset_file_cache(&f,guarded+32,count);
    for(unsigned pass=0;pass<3u;++pass)for(uint32_t i=0;i<count;++i){
        uint32_t index=pass==1u?(i*719u)%count:i,offset=160u+index*128u,n;
        if(fseek(source,(long)offset,SEEK_SET)||fread(expected,1,128,source)!=128||
           r.read_at(r.context,offset,actual,128,&n)!=DOK_ASSET_OK||n!=128||memcmp(expected,actual,128))return 3;
    }
    uint32_t n;f.test_fault_offset=160;f.test_fault_mode=1;
    if(r.read_at(r.context,160,actual,128,&n)!=DOK_ASSET_OK||n!=127)return 4;
    f.test_fault_mode=2;if(r.read_at(r.context,160,actual,128,&n)!=DOK_ASSET_IO_ERROR)return 5;
    for(unsigned i=0;i<32u;++i)if(guarded[i]!=0xa5||guarded[32768+32+i]!=0xa5)return 6;
    printf("{\"passed\":true,\"logical_calls\":%u,\"storage_reads\":%u,\"cache_hits\":%u}\n",f.calls,f.storage_reads,f.cache_hits);
    fclose(source);nds_asset_file_close(&f);return 0;
}
