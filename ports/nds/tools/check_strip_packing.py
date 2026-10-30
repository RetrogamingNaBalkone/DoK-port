"""C14 prove exact strip admission/order after bounded9-bit packing."""
import json,os,re,subprocess
from pathlib import Path
from check_strip_equality import ROOT,E,pin,function
OUT=ROOT/'build/nds/n21p/strip-packing'
def unsigned_function(s,name):
    return function(s.replace('static unsigned '+name+'(','static int '+name+'(',1),name).replace('static int '+name+'(','static unsigned '+name+'(',1)
def main():
    OUT.mkdir(parents=True,exist_ok=True)
    archive=E/'pre-strip-packed/pins.json';items=json.loads(archive.read_text('utf8'))['source_pins']
    for row in items:assert pin(ROOT/row['archive'])['sha256']==row['sha256']
    oldpath=ROOT/items[0]['archive'];path=ROOT/items[0]['path'];old=oldpath.read_text('utf8');new=path.read_text('utf8')
    expected=old.replace('typedef struct StripDraw {unsigned char order[4],reuse;} StripDraw;',
        '/* Four2-bit vertex indices and one reuse bit; capacity is unchanged. */\ntypedef struct StripDraw {uint16_t packed;} StripDraw;\n_Static_assert(sizeof(StripDraw)==2u,"bounded strip metadata");')
    helpers=unsigned_function(new,'strip_order')+'\n'+unsigned_function(new,'strip_reuse')+'\n'
    expected=expected.replace('static StripDraw strip_draw[DOK_NDS_RENDER_POLYGON_LIMIT];\n','static StripDraw strip_draw[DOK_NDS_RENDER_POLYGON_LIMIT];\n'+helpers)
    expected=expected.replace('bd->order[0]=0;bd->order[1]=1;bd->order[2]=3;bd->order[3]=2;bd->reuse=0;','bd->packed=UINT16_C(0x00b4);')
    expected=expected.replace('ad->order[2]','strip_order(ad,2u)').replace('ad->order[3]','strip_order(ad,3u)')
    expected=expected.replace('bd->order[0]=(unsigned char)i;bd->order[1]=(unsigned char)j;\n        bd->order[2]=(unsigned char)((i+(reverse?1u:3u))%4u);\n        bd->order[3]=(unsigned char)((i+2u)%4u);bd->reuse=1;return 1;',
        'bd->packed=(uint16_t)(i|(j<<2u)|(((i+(reverse?1u:3u))%4u)<<4u)|\n            (((i+2u)%4u)<<6u)|256u);return 1;')
    expected=expected.replace('draw->reuse','strip_reuse(draw)').replace('draw->order[k]','strip_order(draw,k)')
    assert expected==new,'Unscoped backend change'
    generated=[]
    for label,source in (('old',old),('new',new)):
        declaration=re.search(r'typedef struct StripDraw .*?\} StripDraw;',source).group()
        body=function(source,'strip_vertex_equal')+'\n'+function(source,'shared_strip')
        if label=='new':body=helpers+body
        text='#include "render_backend.h"\n#include <string.h>\n'+declaration+'\n'+body+'\n'
        if label=='old':
            setup='StripDraw ad={0},bd;if(bytes)memcpy(&ad,bytes,5);memset(&bd,0xa5,sizeof(bd));'
            output='memcpy(out,&bd,5);'
        else:
            setup='StripDraw ad={0},bd={0};if(bytes){for(unsigned k=0;k<4;++k)ad.packed|=(uint16_t)((unsigned)bytes[k]<<(2u*k));ad.packed|=(uint16_t)((unsigned)bytes[4]<<8u);}'
            output='for(unsigned k=0;k<4;++k)out[k]=(unsigned char)strip_order(&bd,k);out[4]=(unsigned char)strip_reuse(&bd);'
        for alias in (False,True):
            name=label+'_strip'+('_alias' if alias else '')
            invocation='bd=ad;int result=shared_strip(a,&bd,b,&bd);' if alias else 'int result=shared_strip(a,bytes?&ad:NULL,b,&bd);'
            text+='int '+name+'(const dok_nds_render_quad *a,const unsigned char *bytes,const dok_nds_render_quad *b,unsigned char *out){'+setup+invocation+output+'return result;}\n'
        if label=='new':
            text+='unsigned new_strip_decode(unsigned bits,const dok_nds_render_quad *q,unsigned char *out,unsigned char *vertices){StripDraw d={(uint16_t)bits};const StripDraw *draw=&d;unsigned n=0;for(unsigned k=0;k<4;++k)out[k]=(unsigned char)strip_order(draw,k);out[4]=(unsigned char)strip_reuse(draw);for(unsigned k=strip_reuse(draw)?2u:0u;k<4;++k){const dok_nds_render_vertex *v=&q->vertex[strip_order(draw,k)];memcpy(vertices+20u*n,v,20);++n;}return n;}\n'
        p=OUT/(label+'.c');p.write_text(text,'utf8');generated.append(p)
    sources=[path,ROOT/'ports/nds/include/dok/render_packet.h',ROOT/'ports/nds/platform/render_probe/render_backend.h',ROOT/'ports/nds/tests/strip_packing_equivalence.c',ROOT/'ports/nds/tests/strip_equality_equivalence.c',Path(__file__)]
    before=[pin(p) for p in sources]
    env=dict(os.environ,ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-global-cache'),ZIG_LOCAL_CACHE_DIR=str(OUT/'zig-cache'))
    zig=Path.home()/'AppData/Local/Programs/Zig/0.13.0/zig.exe';exe=OUT/'strip-packing.exe'
    cmd=[str(zig),'cc','-std=c11','-pedantic-errors','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-O2','-Iports/nds/include','-Iports/nds/platform/render_probe',*[str(p) for p in generated],'ports/nds/tests/strip_packing_equivalence.c','-o',str(exe)]
    subprocess.run(cmd,cwd=ROOT,env=env,check=True)
    result=json.loads(subprocess.check_output([str(exe)],cwd=ROOT,text=True).splitlines()[-1])
    assert result['passed'] and result['helper_cases']==819200 and result['alias_cases']==16384 and result['decode_states']==512
    assert before==[pin(p) for p in sources]
    report=dict(task='N21P C14 packed strip metadata',passed=True,source_pins=before,archived_original=pin(oldpath),archive_manifest=pin(archive),numeric=result,default_packed=180,capacity=2048,old_record_bytes=5,new_record_bytes=2,static_saving_bytes=6144,scope='Actual extracted helpers; old/new return/order/reuse, AD==BD alias, all512 four-index/reuse states and emitted vertex sequence. Backend restoration limits edits to metadata representation/access. Matrix/FIFO/validation/capacity unchanged.',generated=[pin(p) for p in generated],binary=pin(exe),command=cmd)
    (E/'backend-strip-packed-equivalence.json').write_text(json.dumps(report,indent=2)+'\n','utf8');print(json.dumps(result))
if __name__=='__main__':main()
