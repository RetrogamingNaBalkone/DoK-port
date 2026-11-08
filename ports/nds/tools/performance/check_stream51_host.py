"""Execute real old/new stream planners with a bounded SDK allocation model."""
import json
import sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'ports/nds/tools'))
sys.path.insert(0,str(ROOT/'ports/nds/tools/map'))
from check_items import run,artifact
from compiler_cache import compiler_environment
from check_scene_merge import function

PRELUDE=r'''
#include "render_backend.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define N22_STREAM_LARGEST_MISSES 1
#define N22_STREAM_PARTIAL_DEFRAG 0
#define TEXGEN_OFF 0
#define GL_TEXTURE_COLOR0_TRANSPARENT 1
#define GL_RGB256 0
typedef struct ResidentTexture {int sdk_id;uint16_t width,height;} ResidentTexture;
static ResidentTexture resident[128];
static unsigned initialized=1,resident_count,resident_bytes;
static unsigned allocations,deletions,bound,fail_fence;
static struct {unsigned used,w,h,flags;const unsigned char *pixels;} sdk[128];
static int glGenTextures(int n,int *id){assert(n==1);for(unsigned i=0;i<128;++i)if(!sdk[i].used){sdk[i].used=1;*id=(int)i;++allocations;return 1;}return 0;}
static void glBindTexture(int a,int id){(void)a;assert(sdk[id].used);bound=(unsigned)id;}
static int glTexImageNtr2D(int f,unsigned w,unsigned h,unsigned flags,void *a,void *b){(void)f;(void)a;(void)b;sdk[bound].w=w;sdk[bound].h=h;sdk[bound].flags=flags;return 1;}
static int glAssignColorTable(int a,int b){(void)a;(void)b;return 1;}
static void glDeleteTextures(int n,int *id){assert(n==1&&sdk[*id].used);memset(&sdk[*id],0,sizeof(sdk[*id]));++deletions;}
static void *glGetTexturePointer(int id){return (void *)(uintptr_t)(0x06800000u+(unsigned)id*4096u);}
static dok_nds_render_result fence(void *p){(void)p;return fail_fence?DOK_NDS_RENDER_TIMEOUT:DOK_NDS_RENDER_OK;}
static void stream_bulk_begin(void){}
static int pot(unsigned x){return x>=8&&!(x&(x-1));}
'''
HARNESS=r'''
static unsigned char pixels[64][4096];
static uint16_t palette[256];
static dok_nds_stream_texture demand[32];
int main(void){
 unsigned checks=0;
 stream_palette=palette;stream_colors=256;
 for(unsigned frame=0;frame<2000;++frame){
  unsigned count=frame%31+1;
  for(unsigned i=0;i<count;++i){
   unsigned shape=(frame/7+i)%4,w=(shape&1)?32:64,h=(shape&2)?32:64,alpha=(frame/11+i)%2;
   unsigned logical=w-i%3;
   uint64_t key=1u+i+((uint64_t)(frame/2)<<8)+((uint64_t)shape<<48)+((uint64_t)alpha<<52);
   demand[i]=(dok_nds_stream_texture){key,{(uint32_t)key,pixels[i],w*h,palette,(uint16_t)w,(uint16_t)h,(uint16_t)logical,(uint16_t)h,alpha}};
  }
  StreamSlot saved[128];memcpy(saved,stream_slots,sizeof(saved));
  dok_nds_stream_stats stats;
  fail_fence=1;assert(dok_nds_stream_plan(demand,count,&stats)==DOK_NDS_RENDER_TIMEOUT);
  assert(!memcmp(saved,stream_slots,sizeof(saved)));checks+=2;fail_fence=0;
  uint64_t key=demand[0].key;demand[0].key=0;
  assert(dok_nds_stream_plan(demand,count,&stats)==DOK_NDS_RENDER_TEXTURE);demand[0].key=key;++checks;
  assert(dok_nds_stream_plan(demand,count,&stats)==DOK_NDS_RENDER_OK);++checks;
  assert(resident_count==count&&stats.resident_count==count);++checks;
  for(unsigned i=0;i<count;++i){
   unsigned n;for(n=0;n<128;++n)if(stream_slots[n].valid&&stream_slots[n].key==demand[i].key)break;
   assert(n<128);StreamSlot *s=&stream_slots[n];unsigned id=(unsigned)s->sdk_id;
   assert(resident[i].sdk_id==s->sdk_id&&resident[i].width==demand[i].texture.logical_width);
   assert(sdk[id].w==demand[i].texture.storage_width&&sdk[id].h==demand[i].texture.storage_height&&sdk[id].flags==demand[i].texture.color0_transparent);
   if(s->pending)sdk[id].pixels=s->texture.pixels;
   assert(sdk[id].pixels==demand[i].texture.pixels);checks+=4;
  }
  for(unsigned n=0;n<128;++n)stream_slots[n].pending=0;
  stream_planned=0;
 }
 printf("{\"passed\":true,\"checks\":%u,\"allocations\":%u,\"deletions\":%u}\n",checks,allocations,deletions);return 0;
}
'''

def main():
    out=ROOT/'build/nds/n23/stream51-host';out.mkdir(parents=True,exist_ok=True)
    env=compiler_environment(ROOT);results={}
    paths={'baseline':ROOT/'research/evidence/nds/n23/angles51/baseline_render_backend.c',
           'final':ROOT/'research/evidence/nds/n23/candidates/angles51d-ru/sources/ports/nds/platform/render_probe/render_backend.c'}
    for name,path in paths.items():
        text=path.read_text('utf8')
        decl=text[text.index('typedef struct StreamSlot {'):text.index('#ifndef N22_STREAM_LARGEST_MISSES')]
        reset='''dok_nds_render_result dok_nds_stream_reset(const uint16_t *p,uint32_t n){memset(stream_slots,0,sizeof(stream_slots));memset(sdk,0,sizeof(sdk));stream_palette=p;stream_colors=n;return DOK_NDS_RENDER_OK;}\n'''
        code=PRELUDE+decl+reset+'\n'.join(function(text,n)for n in ('stream_pointer_valid','stream_allocate','stream_allocate_missing','dok_nds_stream_plan'))+HARNESS
        source=out/(name+'.c');source.write_text(code,'utf8')
        run([Path.home()/'AppData/Local/Programs/Zig/0.13.0/zig.exe','cc','-std=c11','-O2','-Wall','-Wextra','-Werror',
            '-Iports/nds/include','-Iports/nds/platform/render_probe',source,'-o',out/(name+'.exe')],env=env)
        results[name]=json.loads(run([out/(name+'.exe')],env=env));print(name,results[name],flush=True)
    assert results['baseline']['checks']==results['final']['checks']
    assert results['final']['allocations']<results['baseline']['allocations']/2
    (out/'result.json').write_text(json.dumps(dict(passed=True,results=results,sources=[artifact(p)for p in paths.values()]),indent=2)+'\n','utf8')

if __name__=='__main__':main()
