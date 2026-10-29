/* N06 reference bitmap viewer. This is not the N07 scene renderer. */
#include <nds.h>
#include <fat.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dok/memory_budget.h"
#include "dok/sha256.h"
#include "visual_probe.h"

#define FRAME_BYTES 98304u
#define MAX_CASES 64u
extern unsigned char __end__, __eheap_end;
_Alignas(32) volatile NdsVisualProbe g_nds_visual_probe={
    .magic=NDS_VISUAL_MAGIC,.abi_version=1,.size_bytes=sizeof(NdsVisualProbe)
};
static NdsVisualProbe obs;
static unsigned char records[MAX_CASES][68];
static unsigned char *assets;
static int main_bg, sub_bg;

static uint32_t addr(const void *p) { return (uint32_t)(uintptr_t)p; }
static uint32_t read_le32(const unsigned char *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static void publish(void)
{
    uint32_t sequence;
    ++g_nds_visual_probe.sequence;
    DC_FlushRange((const void *)&g_nds_visual_probe.sequence,32);
    sequence=g_nds_visual_probe.sequence;
    obs.magic=NDS_VISUAL_MAGIC;obs.abi_version=1;obs.size_bytes=sizeof(obs);obs.sequence=sequence;
    if(assets) obs.sdk_heap_available=addr(getHeapLimit())-addr(getHeapEnd());
    memcpy((void *)&g_nds_visual_probe,&obs,sizeof(obs));
    DC_FlushRange((const void *)&g_nds_visual_probe,sizeof(g_nds_visual_probe));
    ++g_nds_visual_probe.sequence;
    DC_FlushRange((const void *)&g_nds_visual_probe.sequence,32);
}
static int setup(void)
{
    uint32_t before=addr(getHeapLimit()),limit=addr(&__eheap_end);
    obs.heap_limit_before=before;obs.static_bytes=addr(&__end__)-DOK_NDS_MAIN_BASE;
    obs.startup_overhead=addr(getHeapEnd())-addr(&__end__);
    obs.reserve_bytes=DOK_NDS_RESERVE_BYTES;
    if(isDSiMode() || limit!=DOK_NDS_ARM9_LIMIT || before<limit || obs.static_bytes>DOK_NDS_STATIC_CEILING ||
       obs.startup_overhead>DOK_NDS_STATIC_CEILING-obs.static_bytes) return 0;
    if(before!=limit && reduceHeapSize(before-limit)!=0) return 0;
    if(reduceHeapSize(DOK_NDS_RESERVE_BYTES)!=0) return 0;
    assets=malloc(DOK_NDS_ASSETS_BYTES);
    if(!assets) return 0;
    obs.asset_pool_bytes=DOK_NDS_ASSETS_BYTES;obs.staging_bytes=2u*FRAME_BYTES;
    obs.top_base=addr(assets);obs.bottom_base=addr(assets+FRAME_BYTES);
    obs.top_bytes=FRAME_BYTES;obs.bottom_bytes=FRAME_BYTES;
    obs.heap_limit_final=addr(getHeapLimit());
    obs.sdk_heap_available=obs.heap_limit_final-addr(getHeapEnd());
    return obs.heap_limit_final==DOK_NDS_ARM9_LIMIT-DOK_NDS_RESERVE_BYTES && obs.sdk_heap_available>=DOK_NDS_SDK_HEAP_MIN;
}
static int read_exact(FILE *file,unsigned char *out,uint32_t bytes)
{
    while(bytes) {
        uint32_t request=bytes>65536u?65536u:bytes;
        size_t got=fread(out,1,request,file);
        ++obs.read_calls;obs.read_bytes+=(uint32_t)got;
        if(request>obs.max_request) obs.max_request=request;
        if(got!=request) return 0;
        bytes-=request;out+=request;
    }
    return 1;
}
static int index_open(void)
{
    unsigned char header[16];uint32_t i,j;
    FILE *file=fopen("fat:/dok/index.dat","rb");
    if(!file) return 0;
    if(!read_exact(file,header,16) || memcmp(header,"N06FRM1\0",8) || read_le32(header+12)!=68u) { fclose(file);return 0; }
    obs.case_count=read_le32(header+8);
    if(!obs.case_count || obs.case_count>MAX_CASES || !read_exact(file,&records[0][0],obs.case_count*68u) || fgetc(file)!=EOF || ferror(file)) { fclose(file);return 0; }
    fclose(file);
    for(i=0;i<obs.case_count;++i) {
        if(!read_le32(records[i])) return 0;
        for(j=0;j<i;++j) if(read_le32(records[i])==read_le32(records[j])) return 0;
    }
    return 1;
}
static int frame_load(const char *path,unsigned char *out,const unsigned char expected[32])
{
    unsigned char actual[32];dok_sha256 hash;uint32_t i;
    FILE *file=fopen(path,"rb");
    if(!file) return 0;
    if(!read_exact(file,out,FRAME_BYTES) || fgetc(file)!=EOF || ferror(file)) { fclose(file);return 0; }
    fclose(file);
    dok_sha256_begin(&hash);dok_sha256_update(&hash,out,FRAME_BYTES);dok_sha256_end(&hash,actual);
    if(memcmp(actual,expected,32)) return 0;
    for(i=1;i<FRAME_BYTES;i+=2) if(!(out[i]&128u)) return 0;
    return 1;
}
static int present(uint32_t ordinal)
{
    char path[40];
    obs.status=0;publish();
    snprintf(path,sizeof(path),"fat:/dok/v%02lutop.bin",(unsigned long)ordinal);
    if(!frame_load(path,assets,records[ordinal]+4)) return 0;
    snprintf(path,sizeof(path),"fat:/dok/v%02lubot.bin",(unsigned long)ordinal);
    if(!frame_load(path,assets+FRAME_BYTES,records[ordinal]+36)) return 0;
    DC_FlushRange(assets,2u*FRAME_BYTES);
    swiWaitForVBlank();
    dmaCopy(assets,bgGetGfxPtr(main_bg),FRAME_BYTES);
    dmaCopy(assets+FRAME_BYTES,bgGetGfxPtr(sub_bg),FRAME_BYTES);
    obs.ordinal=ordinal;obs.case_id=read_le32(records[ordinal]);++obs.displayed;
    obs.status=1;publish();return 1;
}
int main(void)
{
    videoSetMode(MODE_5_2D);videoSetModeSub(MODE_5_2D);
    vramSetBankA(VRAM_A_MAIN_BG);vramSetBankC(VRAM_C_SUB_BG);lcdMainOnTop();
    main_bg=bgInit(3,BgType_Bmp16,BgSize_B16_256x256,0,0);
    sub_bg=bgInitSub(3,BgType_Bmp16,BgSize_B16_256x256,0,0);
    if(!setup()) obs.error=1;
    else if(!fatInitDefault()) obs.error=2;
    else if(!index_open()) obs.error=3;
    else if(!present(0)) obs.error=4;
    if(obs.error) obs.status=2;
    for(;;) {
        swiWaitForVBlank();++obs.frame;scanKeys();
        if(obs.status==1 && (keysDown()&KEY_A)) {
            if(obs.ordinal+1u<obs.case_count) {
                if(!present(obs.ordinal+1u)) {obs.error=4;obs.status=2;}
            } else obs.status=3;
        }
        publish();
    }
}
