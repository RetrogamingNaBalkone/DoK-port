"""N19 actual UI language callback and simulation sprite-ID isolation gate."""
from __future__ import annotations
import hashlib,json,os,subprocess,sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'build/nds/n19/ui-language-runtime'
EVIDENCE=ROOT/'research/evidence/nds/n19'

def artifact(path):
    p=Path(path);data=p.read_bytes()
    return dict(path=p.relative_to(ROOT).as_posix(),bytes=len(data),sha256=hashlib.sha256(data).hexdigest())

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    stub=OUT/'nds.h'
    stub.write_text('''#ifndef N19_HOST_NDS_H
#define N19_HOST_NDS_H
#include <stdint.h>
#include <string.h>
extern uint16_t n19_test_palette[256];
#define BG_PALETTE n19_test_palette
static inline void DC_FlushRange(const void *p,uint32_t n){(void)p;(void)n;}
static inline void dmaCopy(const void *a,void *b,uint32_t n){memcpy(b,a,n);}
#endif
''','ascii')
    sources=['ports/nds/core/'+n+'.c' for n in ('assets','world','balance','sha256')]
    sources+=['ports/nds/render/sprites.c','ports/nds/render/sprite_assets.c','ports/nds/render/scene.c','ports/nds/render/geometry.c',
        'ports/nds/ui/hud.c','ports/nds/platform/assets/file_reader.c','ports/nds/tests/test_ui_language_runtime.c']
    runtime=ROOT/'ports/nds/platform/ui/runtime.c'
    watched=[ROOT/p for p in sources]+[runtime,ROOT/'ports/nds/platform/ui/runtime.h',ROOT/'ports/nds/include/dok/balance.h',
        ROOT/'ports/nds/tools/check_ui_language_runtime.py',ROOT/'build/nds/n19/ui.dat',ROOT/'build/nds/n19/generated/ui_native.inc',
        ROOT/'build/nds/assets/dok-assets.ndspak',ROOT/'build/nds/n07/generated/render_trig.inc']
    before=[artifact(p) for p in watched]
    zig=Path.home()/'AppData/Local/Programs/Zig/0.13.0/zig.exe'
    flags=['-std=c11','-pedantic-errors','-Wall','-Wextra','-Werror','-O2','-ffunction-sections',
        '-I',OUT,'-I','ports/nds/include','-I','ports/nds/platform/assets','-I','ports/nds/platform/ui','-I','build/nds/n19/generated',
        '-I','build/nds/n07/generated']
    env=dict(os.environ,ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-global-cache'),ZIG_LOCAL_CACHE_DIR=str(ROOT/'build/zig-local-cache'))
    report=dict(task='N19',passed=False,scope='Actual runtime language/message/update C, host nds cache/DMA/palette stubs; no hardware claims',commands=[])
    def run(args,allowed_fail=False):
        r=subprocess.run(list(map(str,args)),cwd=ROOT,env=env,capture_output=True,text=True,encoding='utf8')
        report['commands'].append(dict(argv=list(map(str,args)),exit_code=r.returncode,stdout=r.stdout,stderr=r.stderr))
        if r.returncode and not allowed_fail:raise RuntimeError(r.stdout+r.stderr)
        return r
    command=[zig,'cc',*flags,*sources,runtime,'-o',OUT/'test_ui_language.exe']
    run(command)
    inputs=[ROOT/'build/nds/assets/dok-assets.ndspak',ROOT/'build/nds/n19/ui.dat']
    report['actual_runtime']=json.loads(run([OUT/'test_ui_language.exe',*inputs]).stdout)
    original=runtime.read_text('utf8')
    # The old two-second lifetime must fail the independent boundary test.
    assert original.count('#define NOTICE_PAGE_TICKS 40107u')==1
    short=OUT/'short-notice-runtime.c';short.write_text(original.replace('#define NOTICE_PAGE_TICKS 40107u','#define NOTICE_PAGE_TICKS 20054u'),'utf8')
    run([zig,'cc',*flags,*sources,short,'-o',OUT/'test_short_notice.exe'])
    rejected_short=run([OUT/'test_short_notice.exe',*inputs],allowed_fail=True)
    assert rejected_short.returncode!=0 and 'ui.message_kind==2&&ui.message_id==0&&ui.page==0' in rejected_short.stderr
    report['notice_mutation_control']=dict(rejected=True,fault='Old two-second notice lifetime',exit_code=rejected_short.returncode,stderr=rejected_short.stderr.strip(),source=artifact(short))
    old='&scratch->presentation,&u->presentation)'
    assert original.count(old)==1,'Expected exact current callback publication site'
    # Restore the old publication destination while keeping the new UI
    # descriptor populated, so the test reaches the actual EN switch. Remove
    # the new internal assertion too: independent world/bank checks must catch
    # the old fault, rather than relying on the production assertion itself.
    faulty_source=original.replace(old,'&scratch->presentation,&w->presentation)')
    publication='!=DOK_BALANCE_OK)return 0;'
    assert faulty_source.count(publication)==1
    faulty_source=faulty_source.replace(publication,publication+'\n    u->presentation=w->presentation;')
    assertion='if(memcmp(u->simulation_before,u->simulation_after,32))return 0;'
    assert faulty_source.count(assertion)==1
    faulty_source=faulty_source.replace(assertion,'/* Mutant removes the new guard; external world check must fail. */')
    faulty=OUT/'faulty-runtime.c';faulty.write_text(faulty_source,'utf8')
    run([zig,'cc',*flags,*sources,faulty,'-o',OUT/'test_faulty_ui_language.exe'])
    rejected=run([OUT/'test_faulty_ui_language.exe',*inputs],allowed_fail=True)
    assert rejected.returncode!=0 and 'actual language line' in rejected.stderr,'Old presentation publication fault escaped gate'
    report['mutation_control']=dict(rejected=True,fault='UI bind publishes into world.presentation',
        exit_code=rejected.returncode,stderr=rejected.stderr.strip(),stdout=rejected.stdout.strip(),source=artifact(faulty))
    assert [artifact(p) for p in watched]==before,'Source/input changed during runtime check'
    report['sources_and_inputs']=before;report['nds_stub']=artifact(stub)
    report['artifacts']=[artifact(p) for p in sorted(OUT.glob('*.exe'))];report['passed']=True
    target=EVIDENCE/'ui-language-runtime-check.json';target.write_text(json.dumps(report,indent=2)+'\n','utf8')
    summary={k:v for k,v in report.items() if k not in ('commands','sources_and_inputs','artifacts','nds_stub')}
    summary['report']=artifact(target)
    (ROOT/'analysis/nds-n19-ui-language-runtime.json').write_text(json.dumps(summary,indent=2)+'\n','utf8')
    print(json.dumps(summary,indent=2))

if __name__=='__main__':main()
