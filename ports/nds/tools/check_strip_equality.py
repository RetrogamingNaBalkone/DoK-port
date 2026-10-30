"""C12 compare exact extracted strip helpers; no SDK/guest execution."""
import hashlib,json,os,re,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
E=ROOT/'research/evidence/nds/n21p'
OUT=ROOT/'build/nds/n21p/strip-equality'
def pin(p):
    b=p.read_bytes();return dict(path=p.relative_to(ROOT).as_posix(),bytes=len(b),sha256=hashlib.sha256(b).hexdigest())
def function(s,name):
    start=s.index('static int '+name+'(');at=s.index('{',start);depth=1;end=at+1
    while depth:
        if s[end]=='{':depth+=1
        elif s[end]=='}':depth-=1
        end+=1
    return s[start:end]
def main():
    OUT.mkdir(parents=True,exist_ok=True)
    pins=json.loads((E/'pre-backend-typed/pins.json').read_text('utf8'))['source_pins']
    for p in pins:assert pin(ROOT/p['archive'])['sha256']==p['sha256']
    oldpath=ROOT/pins[0]['archive'];path=ROOT/pins[0]['path']
    old=oldpath.read_text('utf8');new=path.read_text('utf8')
    begin=new.index('_Static_assert(sizeof(dok_nds_render_vertex)');end=new.index('static int shared_strip(',begin)
    restored=new[:begin]+new[end:]
    restored=restored.replace('!strip_vertex_equal(&a->vertex[ad->order[2]],&b->vertex[i])','memcmp(&a->vertex[ad->order[2]],&b->vertex[i],sizeof(b->vertex[i]))')
    restored=restored.replace('!strip_vertex_equal(&a->vertex[ad->order[3]],&b->vertex[j])','memcmp(&a->vertex[ad->order[3]],&b->vertex[j],sizeof(b->vertex[j]))')
    assert restored==old,'Unscoped backend edit'
    declaration=re.search(r'typedef struct StripDraw .*?\} StripDraw;',old).group()
    generated=[]
    for label,source in (('old',old),('new',new)):
        body=function(source,'shared_strip')
        if label=='new':body=function(source,'strip_vertex_equal')+'\n'+body
        text='#include "render_backend.h"\n#include <string.h>\n'+declaration+'\n_Static_assert(sizeof(StripDraw)==5,"strip byte layout");\n_Static_assert(sizeof(dok_nds_render_vertex)==20,"vertex no padding");\n'+body
        text+='\nint '+label+'_strip(const dok_nds_render_quad *a,const unsigned char *bytes,const dok_nds_render_quad *b,unsigned char *out){StripDraw ad,bd; if(bytes)memcpy(&ad,bytes,5);memset(&bd,0xa5,5);int result=shared_strip(a,bytes?&ad:NULL,b,&bd);memcpy(out,&bd,5);return result;}\n'
        q=OUT/(label+'.c');q.write_text(text,'utf8');generated.append(q)
    sources=[path,ROOT/'ports/nds/include/dok/render_packet.h',ROOT/'ports/nds/platform/render_probe/render_backend.h',ROOT/'ports/nds/tests/strip_equality_equivalence.c',Path(__file__)]
    before=[pin(p) for p in sources]
    env=dict(os.environ,ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-global-cache'),ZIG_LOCAL_CACHE_DIR=str(OUT/'zig-cache'))
    zig=Path.home()/'AppData/Local/Programs/Zig/0.13.0/zig.exe';exe=OUT/'strip-equivalence.exe'
    cmd=[str(zig),'cc','-std=c11','-pedantic-errors','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-O2','-Iports/nds/include','-Iports/nds/platform/render_probe',*[str(p) for p in generated],'ports/nds/tests/strip_equality_equivalence.c','-o',str(exe)]
    subprocess.run(cmd,cwd=ROOT,env=env,check=True)
    result=json.loads(subprocess.check_output([str(exe)],cwd=ROOT,text=True));assert result['passed'] and result['cases']==819200
    assert before==[pin(p) for p in sources]
    report=dict(task='N21P C12 exact typed strip endpoint equality',passed=True,source_pins=before,archived_original=pin(oldpath),scope='Only equal comparison of five int32 fields replaces memcmp20; vertices contain exactly20 bytes with no padding. Matrix/FIFO/validation/capacity code remains text-identical.',extraction='Exact actual shared_strip and strip_vertex_equal C bodies, independently compiled; SDK is not needed by these pure helpers.',numeric=result,domains=['all24 previous vertex permutations; all8 next edge orientations','full signed32 random coordinates and UVs; boundary values','matched and mismatched endpoint components, degenerate vertices, ignored flags/reserved','null previous quad, texture/color mismatches, unchanged inputs, five output bytes with tail guards'],generated=[pin(p) for p in generated],binary=pin(exe),command=cmd)
    (E/'backend-strip-equivalence.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    print(json.dumps(result))
if __name__=='__main__':main()
