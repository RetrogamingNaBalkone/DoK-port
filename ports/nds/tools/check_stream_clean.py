"""C17 actual stream blocks: public contract + prepared cache/DMA ordering."""
import json,os,re,subprocess
from pathlib import Path
from check_strip_equality import ROOT,E,pin
OUT=ROOT/'build/nds/n21p/stream-clean'

def function(source,name):
    match=re.search(r'^(?:static )?dok_nds_render_result '+name+r'\(',source,re.M);assert match,name
    start=match.start();at=source.index('{',start);depth=1;end=at+1
    while depth:
        if source[end]=='{':depth+=1
        elif source[end]=='}':depth-=1
        end+=1
    return source[start:end]

def main():
    OUT.mkdir(parents=True,exist_ok=True);archive=E/'pre-stream-clean/pins.json';rows=json.loads(archive.read_text('utf8'))['source_pins']
    for row in rows:assert pin(ROOT/row['archive'])['sha256']==row['sha256']
    path=ROOT/rows[1]['path'];old=(ROOT/rows[1]['archive']).read_text('utf8');new=path.read_text('utf8')
    oldbody=function(old,'dok_nds_stream_commit');kernel=function(new,'stream_commit')
    restored=kernel.replace('static dok_nds_render_result stream_commit(dok_nds_stream_stats *stats,uint32_t clean_sources)','dok_nds_render_result dok_nds_stream_commit(dok_nds_stream_stats *stats)').replace('if(clean_sources)DC_FlushRange','DC_FlushRange')
    assert restored==oldbody
    start=new.index('dok_nds_render_result dok_nds_stream_prepare_uploads(void)');assert new[:start]==old[:old.index('dok_nds_render_result dok_nds_stream_commit(')]
    assert new.rstrip().endswith('{return stream_commit(stats,0u);}') and old.rstrip().endswith(oldbody)
    mainpath=ROOT/rows[0]['path'];main=mainpath.read_text('utf8');oldmain=(ROOT/rows[0]['archive']).read_text('utf8')
    begin=main.index('    uint32_t cold=!stream_ready,stream_prepare_ticks=0;');end=main.index('    if(cold) {',begin)
    restoredmain=main[:begin]+'    phase=cpuGetTiming();\n    uint32_t cold=!stream_ready;\n'+main[end:]
    restoredmain=restoredmain.replace('dok_nds_stream_commit_prepared(&stream_stats)','dok_nds_stream_commit(&stream_stats)').replace('stream_prepare_ticks+cpuGetTiming()-phase','cpuGetTiming()-phase')
    assert restoredmain==oldmain,'Unscoped main change'
    assert main.index('nds_map_prepare(map_ui')<begin<main.index('if(dok_nds_render_submit(frame')<main.index('dok_nds_stream_commit_prepared(&stream_stats)')
    generated=[]
    for label,source in [('old',old),('new',new)]:
        declaration=source[source.index('typedef struct StreamSlot {'):source.index('static uint32_t stream_repacking;')]
        body=oldbody if label=='old' else function(new,'dok_nds_stream_prepare_uploads')+'\n'+kernel+'\n'+function(new,'dok_nds_stream_commit')+'\n'+function(new,'dok_nds_stream_commit_prepared')
        text='''#include "stream_clean_equivalence.h"
#include <string.h>
#include <stdint.h>
#define VRAM_A_LCD 1
#define VRAM_B_LCD 2
#define VRAM_C_SUB_BG 3
#define VRAM_D_LCD 4
#define VRAM_F_LCD 5
#define POWER_3D_CORE 1
#define GL_TEXTURE_2D 1
static unsigned REG_VCOUNT,loading;
static uint8_t VRAM_F_CR;
static unsigned char pixels[128][64],clean[128][64],tex[128][64],pal[8192],cleanpal[8192],gpu_pal[8192],bg[512];
#define BG_PALETTE bg
static unsigned flushed[128],palette_clean,dmas,flushes,fault;
static void DC_FlushRange(const void *p,unsigned bytes){++flushes;if(p==pal){memcpy(cleanpal,p,bytes);palette_clean=1;return;}for(unsigned i=0;i<128;++i)if(p==pixels[i]){memcpy(clean[i],p,bytes);flushed[i]=1;return;}++fault;}
static void dmaCopy(const void *p,void *out,unsigned bytes){++dmas;if(p==pal){if(!palette_clean)++fault;memcpy(out,cleanpal,bytes);return;}for(unsigned i=0;i<128;++i)if(p==pixels[i]){if(!flushed[i])++fault;memcpy(out,clean[i],bytes);return;}++fault;}
static unsigned vramSetPrimaryBanks(unsigned a,unsigned b,unsigned c,unsigned d){(void)a;(void)b;(void)c;(void)d;return 11;}
static void vramRestorePrimaryBanks(unsigned x){if(x!=11)++fault;}
static void vramSetBankF(unsigned x){VRAM_F_CR=(uint8_t)x;}
static void *glGetTexturePointer(int id){return tex[(unsigned)id];}
static void *glGetColorTablePointer(int id){(void)id;return gpu_pal;}
static void commit_darkness(void){}
static void powerOn(unsigned x){(void)x;}
static void set_control(unsigned x){(void)x;}
'''+declaration+'\n'+body
        text+='\nvoid '+label+'''_case(unsigned seed,unsigned prepared,stream_result *out){
memset(out,0,sizeof(*out));memset(stream_slots,0,sizeof(stream_slots));memset(tex,0x5a,sizeof(tex));memset(gpu_pal,0x6b,sizeof(gpu_pal));memset(bg,0x7c,sizeof(bg));memset(flushed,0,sizeof(flushed));palette_clean=dmas=flushes=fault=0;
const unsigned lines[8]={0,47,48,191,192,224,250,262};REG_VCOUNT=lines[seed&7u];loading=(seed>>3)&1u;stream_planned=(seed%7u)!=0;stream_palette_pending=(seed>>4)&1u;stream_colors=(seed&32u)?4096:256;stream_palette=(const uint16_t *)(const void *)pal;stream_palette_id=0;VRAM_F_CR=17;
for(unsigned i=0;i<8192;++i)pal[i]=(unsigned char)(seed+i);
for(unsigned i=0;i<128;++i){StreamSlot *s=&stream_slots[i];s->valid=((seed+i)%3u)!=0;s->pending=((seed+i)%5u)!=0;s->sdk_id=(int)i;s->texture.pixels=pixels[i];s->texture.bytes=64;for(unsigned j=0;j<64;++j)pixels[i][j]=(unsigned char)(seed+i+j);}
dok_nds_stream_stats stats;memset(&stats,0xa5,sizeof(stats));dok_nds_stream_stats *ptr=seed%11u?&stats:NULL;
out->prepare_state_unchanged=1;
'''
        if label=='new':
            text+='if(prepared){unsigned old_planned=stream_planned,old_palette=stream_palette_pending;out->prepare_result=dok_nds_stream_prepare_uploads();if(stream_planned!=old_planned||stream_palette_pending!=old_palette||dmas)out->prepare_state_unchanged=0;for(unsigned i=0;i<128;++i)if(stream_slots[i].pending!=(((seed+i)%5u)!=0))out->prepare_state_unchanged=0;}\n'
            text+='out->result=prepared?dok_nds_stream_commit_prepared(ptr):dok_nds_stream_commit(ptr);\n'
        else:text+='(void)prepared;out->result=dok_nds_stream_commit(ptr);\n'
        text+='memcpy(out->texture,tex,sizeof(tex));memcpy(out->palette,gpu_pal,sizeof(gpu_pal));memcpy(out->background,bg,sizeof(bg));for(unsigned i=0;i<128;++i)out->pending[i]=stream_slots[i].pending;out->palette_pending=stream_palette_pending;out->planned=stream_planned;out->dmas=dmas;out->flushes=flushes;out->fault=fault;out->stats=stats;}\n'
        aliases=''.join('#define '+symbol+' '+label+'_'+symbol+'\n' for symbol in ('dok_nds_stream_commit','dok_nds_stream_prepare_uploads','dok_nds_stream_commit_prepared'))
        p=OUT/(label+'.c');p.write_text(aliases+text,'utf8');generated.append(p)
    sources=[ROOT/row['path'] for row in rows]+[ROOT/'ports/nds/tests/stream_clean_equivalence.c',ROOT/'ports/nds/tests/stream_clean_equivalence.h',Path(__file__)]
    before=[pin(p) for p in sources];env=dict(os.environ,ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-global-cache'),ZIG_LOCAL_CACHE_DIR=str(OUT/'zig-cache'));zig=Path.home()/'AppData/Local/Programs/Zig/0.13.0/zig.exe';exe=OUT/'stream-clean.exe'
    cmd=[str(zig),'cc','-std=c11','-pedantic-errors','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-O2','-Iports/nds/include','-Iports/nds/platform/render_probe','-Iports/nds/tests',*[str(p) for p in generated],'ports/nds/tests/stream_clean_equivalence.c','-o',str(exe)]
    subprocess.run(cmd,cwd=ROOT,env=env,check=True);result=json.loads(subprocess.check_output([str(exe)],cwd=ROOT,text=True));assert result['passed']
    assert before==[pin(p) for p in sources]
    report=dict(task='N21P C17 pre-swap source cleaning',passed=True,numeric=result,source_pins=before,archive=pin(archive),generated=[pin(p) for p in generated],binary=pin(exe),command=cmd,scope='Actual old commit/shared new kernel and public/prepared wrappers; public byte/state/error parity, prepared cache model enforces flush-before-DMA, immutable pending/palette plan after prepare, actual main order/source restoration. VCOUNT duration and LCD visibility require root runtime.')
    (E/'stream-clean-equivalence.json').write_text(json.dumps(report,indent=2)+'\n','utf8');print(json.dumps(result))
if __name__=='__main__':main()
