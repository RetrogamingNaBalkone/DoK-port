"""Actual planners: cold/warm animation cycles, pressure, expired CPU leases."""
import json
import sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'ports/nds/tools'))
sys.path.insert(0,str(ROOT/'ports/nds/tools/map'))
from check_items import run,artifact
from compiler_cache import compiler_environment
from check_scene_merge import function
from check_stream51_host import PRELUDE,HARNESS


def main():
    out=ROOT/'build/nds/n23/stream53-host';out.mkdir(parents=True,exist_ok=True)
    env=compiler_environment(ROOT);results={}
    paths={'baseline':ROOT/'research/evidence/nds/n23/resident53/baseline_render_backend.c',
           'final':ROOT/'ports/nds/platform/render_probe/render_backend.c'}
    prelude=PRELUDE.replace('i<128;++i)if(!sdk[i].used)','i<96;++i)if(!sdk[i].used)')
    prelude=prelude.replace('static unsigned allocations,deletions,bound,fail_fence;',
        'static unsigned allocations,deletions,bound,fail_fence,fail_alloc,resets;')
    prelude=prelude.replace('const unsigned char *pixels;} sdk',
        'const unsigned char *pixels;uint64_t copied_token;} sdk')
    prelude=prelude.replace('assert(n==1);for(unsigned i=0;',
        'assert(n==1);if(fail_alloc){--fail_alloc;return 0;}for(unsigned i=0;')
    prelude=prelude.replace('0x06800000u+(unsigned)id*4096u',
        '((unsigned)id<64u?0x06800000u+(unsigned)id*4096u:0x06860000u+((unsigned)id-64u)*4096u)')
    harness=HARNESS.replace('stats.resident_count==count','stats.resident_count>=count')
    harness=harness.replace(' for(unsigned frame=0;frame<2000;++frame){',
        ' unsigned warm_uploads=0,pressure_uploads=0,rewarm_uploads=0;\n for(unsigned frame=0;frame<4000;++frame){')
    harness=harness.replace('unsigned count=frame%31+1;',
        'unsigned count=frame<1000?16:frame%31+1;')
    harness=harness.replace('(frame/7+i)%4','(frame<1000?i:(frame/7+i))%4')
    harness=harness.replace('(frame/11+i)%2','(frame<1000?i:(frame/11+i))%2')
    harness=harness.replace('(frame/2)<<8','(frame<1000?frame%3:frame/2)<<8')
    harness=harness.replace('unsigned logical=w-i%3;',
        'if(frame==1500){w=h=8;} unsigned logical=w-i%3;')
    harness=harness.replace('uint64_t key=1u+i+',
        'uint64_t key=(frame==1500?UINT64_C(1)<<56:0)+1u+i+')
    harness=harness.replace('  assert(dok_nds_stream_plan(demand,count,&stats)==DOK_NDS_RENDER_OK);',
        '  if(frame==1500)fail_alloc=ALLOCATION_FAILURES;\n  assert(dok_nds_stream_plan(demand,count,&stats)==DOK_NDS_RENDER_OK);')
    needle='  assert(resident_count==count&&stats.resident_count>=count);++checks;'
    assert harness.count(needle)==1
    harness=harness.replace(needle,needle+'''
  assert(stats.resident_count<=96&&stats.resident_bytes<=DOK_NDS_RENDER_TEXTURE_BYTES);++checks;
  if(frame>=10&&frame<1000)warm_uploads+=stats.uploads;
  if(frame>=1000)pressure_uploads+=stats.uploads;
''')
    harness=harness.replace('  stream_planned=0;','''
  for(unsigned n=0;n<128;++n)if(stream_slots[n].valid&&!stream_slots[n].pinned)
      stream_slots[n].texture.pixels=(const unsigned char *)(uintptr_t)1;
  stream_planned=0;''')
    harness=harness.replace('  StreamSlot saved[128];',
        '  for(unsigned i=0;i<count;++i)memcpy(pixels[i],&demand[i].key,8);\n  StreamSlot saved[128];')
    harness=harness.replace('if(s->pending)sdk[id].pixels=s->texture.pixels;',
        'if(s->pending){sdk[id].pixels=s->texture.pixels;memcpy(&sdk[id].copied_token,s->texture.pixels,8);}')
    harness=harness.replace('assert(sdk[id].pixels==demand[i].texture.pixels);checks+=4;',
        'assert(sdk[id].pixels==demand[i].texture.pixels&&sdk[id].copied_token==demand[i].key);checks+=4;')
    harness=harness.replace('allocations,deletions);return 0;',
        'allocations,deletions);assert(resets>0);printf("{\\\"warm_uploads\\\":%u,\\\"pressure_uploads\\\":%u,\\\"rewarm_uploads\\\":%u,\\\"resets\\\":%u}\\n",warm_uploads,pressure_uploads,rewarm_uploads,resets);return 0;')
    harness=harness.replace('frame<1000','(frame<1000||frame>=3000)')
    harness=harness.replace('frame>=10&&(frame<1000||frame>=3000)','frame>=10&&frame<1000')
    harness=harness.replace('if(frame>=1000)pressure_uploads+=stats.uploads;',
        'if(frame>=1000&&frame<3000)pressure_uploads+=stats.uploads;\n  if(frame>=3100)rewarm_uploads+=stats.uploads;')
    for name,path in paths.items():
        text=path.read_text('utf8')
        decl=text[text.index('typedef struct StreamSlot {'):text.index('#ifndef N22_STREAM_LARGEST_MISSES')]
        reset='''dok_nds_render_result dok_nds_stream_reset(const uint16_t *p,uint32_t n){++resets;memset(stream_slots,0,sizeof(stream_slots));memset(sdk,0,sizeof(sdk));stream_palette=p;stream_colors=n;return DOK_NDS_RENDER_OK;}\n'''
        if name=='final':reset=reset.replace('++resets;','++resets;stream_recycle_cursor=0;')
        names=['stream_pointer_valid','stream_allocate','stream_allocate_missing']
        if name=='final':names.append('stream_recycle_retired')
        names.append('dok_nds_stream_plan')
        code=f'#define ALLOCATION_FAILURES {3 if name=="final" else 1}\n'+prelude+decl+reset+'\n'.join(function(text,n)for n in names)+harness
        source=out/(name+'.c');source.write_text(code,'utf8')
        run([Path.home()/'AppData/Local/Programs/Zig/0.13.0/zig.exe','cc','-std=c11','-O2','-Wall','-Wextra','-Werror',
            '-Iports/nds/include','-Iports/nds/platform/render_probe',source,'-o',out/(name+'.exe')],env=env)
        rows=[json.loads(s)for s in run([out/(name+'.exe')],env=env).splitlines()]
        results[name]=rows[0]|rows[1];print(name,results[name],flush=True)
    assert results['baseline']['checks']==results['final']['checks']
    assert results['baseline']['warm_uploads']>0 and results['final']['warm_uploads']==0
    assert results['final']['pressure_uploads']>0
    assert results['final']['rewarm_uploads']==0 and results['baseline']['rewarm_uploads']>0
    (out/'result.json').write_text(json.dumps(dict(passed=True,results=results,
        sources=[artifact(p)for p in paths.values()],scope=__doc__),indent=2)+'\n','utf8')


if __name__=='__main__':main()
