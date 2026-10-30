"""C18 conservative offscreen map-cell rejection: exact source/host/native proof."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
from check_scene_merge import function

ROOT=Path(__file__).resolve().parents[4]
OUT=ROOT/'build/nds/n21p/map-cull'
EVIDENCE=ROOT/'research/evidence/nds/n21p'

def pin(path):
    path=Path(path)
    return dict(path=path.relative_to(ROOT).as_posix(),bytes=path.stat().st_size,
                sha256=hashlib.sha256(path.read_bytes()).hexdigest())

def originals():
    result={}
    for language in ('ru','en'):
        m=json.loads((ROOT/f'research/evidence/automap/{language}/manifest.json').read_text('utf8'))
        result[language]=pin(ROOT/m['sample'])
        assert result[language]['sha256']==m['sha256_before']==m['sha256_after']
    return result

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--host-only',action='store_true')
    ap.add_argument('--native-only',action='store_true')
    args=ap.parse_args()
    assert not(args.host_only and args.native_only)
    OUT.mkdir(parents=True,exist_ok=True)
    saved=json.loads((EVIDENCE/'pre-map-cull/pins.json').read_text('utf8'))['source_pins']
    for p in saved:assert pin(ROOT/p['archive'])['sha256']==p['sha256']
    old=ROOT/next(p['archive'] for p in saved if p['path']=='ports/nds/ui/map_raster.c')
    current=ROOT/'ports/nds/ui/map_raster.c'
    old_text=old.read_text('utf8');new_text=current.read_text('utf8')
    row='''        if (top > 159 || top + (int32_t)scale < 0) {
            index += (int)s->width; continue;
        }
'''
    column='''            right = offset_x + (int32_t)(s->width - x) * (int32_t)scale;
            left = right - (int32_t)scale;
            if (right < 0 || left > 255) continue;
'''
    assert new_text.count(row)==2 and new_text.count(column)==2
    restored=new_text.replace(row,'').replace(column,'')
    restored=restored.replace('            rect(out, left, top, right, top + (int32_t)scale, colour);',
        '''            right = offset_x + (int32_t)(s->width - x) * (int32_t)scale;
            rect(out, right - (int32_t)scale, top, right, top + (int32_t)scale, colour);''',1)
    restored=restored.replace('            if (class_id == 2) {',
        '''            right = offset_x + (int32_t)(s->width - x) * (int32_t)scale;
            left = right - (int32_t)scale;
            if (class_id == 2) {''',1)
    assert restored==old_text,'change exceeds four bounds checks and equivalent coordinate reordering'
    unchanged=('pixel','rect','native_class','seen','blocks_line')
    for name in unchanged:assert function(old_text,name)==function(new_text,name)
    footer='    /* Drawing once after the traversal'
    assert new_text[new_text.index(footer):]==old_text[old_text.index(footer):]
    accepted=ROOT/'research/evidence/nds/n20/raster-check.json'
    n20=json.loads(accepted.read_text('utf8'))
    original_source=ROOT/'research/evidence/nds/n21p/pre-map-unroll/ports/nds/ui/map_raster.c'
    assert pin(original_source)['sha256']==n20['source_sha256']['ports/nds/ui/map_raster.c']
    stripped=old_text.replace('#include "dok/hotcode.h"\n','').replace('DOK_NDS_HOT_CODE ','')
    assert stripped==original_source.read_text('utf8'),'baseline must retain accepted N20 algorithm'
    sources=['ports/nds/ui/map_raster.c','ports/nds/include/dok/map_raster.h','ports/nds/include/dok/hotcode.h',
             'ports/nds/tests/map_cull_equivalence.c','ports/nds/tools/map/check_map_cull.py',
             'ports/nds/tools/check_map_raster.py','ports/nds/tools/map/original_map.py']
    before=[pin(ROOT/p) for p in sources];original_before=originals()
    env=os.environ.copy();env['ZIG_LOCAL_CACHE_DIR']=str(OUT/'zig-local-cache');env['ZIG_GLOBAL_CACHE_DIR']=str(OUT/'zig-global-cache')
    zig=Path(os.environ['LOCALAPPDATA'])/'Programs/Zig/0.13.0/zig.exe'
    flags=[str(zig),'cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-pedantic','-Iports/nds/include']
    report=EVIDENCE/'map-cull-equivalence.json'
    if not args.native_only:
        baseline=OUT/'baseline_raster.c'
        baseline.write_bytes(old.read_bytes()+b'\nvoid baseline_rect(uint8_t *o,int32_t x,int32_t y,int32_t a,int32_t b,uint8_t c){rect(o,x,y,a,b,c);}\n')
        commands=[flags+['-Ddok_map_raster=baseline_dok_map_raster','-c',str(baseline),'-o',str(OUT/'baseline.o')],
                  flags+['ports/nds/tests/map_cull_equivalence.c',str(OUT/'baseline.o'),'-o',str(OUT/'map_cull_equivalence.exe')]]
        for command in commands:subprocess.run(command,cwd=ROOT,env=env,check=True)
        differential=json.loads(subprocess.check_output([str(OUT/'map_cull_equivalence.exe')],cwd=ROOT,text=True))
        assert differential['passed'] and differential['alignments']==4 and differential['hero_escape_cases']==8
        snapshot=EVIDENCE/'c18-map/ports/nds/ui/map_raster.c';snapshot.parent.mkdir(parents=True,exist_ok=True)
        if snapshot.exists():assert snapshot.read_bytes()==current.read_bytes()
        else:snapshot.write_bytes(current.read_bytes())
        result=dict(task='N21P C18 exact offscreen map-cell rejection',passed=True,source_pins=before,
                    source_pins_before_after_equal=True,archived_baseline=pin(old),current_snapshot=pin(snapshot),
                    differential=differential,whole_source_restoration=True,row_checks=2,column_checks=2,
                    unchanged_helpers={n:hashlib.sha256(function(new_text,n).encode()).hexdigest() for n in unchanged},
                    hero_footer_unchanged=True,rect_inclusive_clipping_unchanged=True,
                    rejection_proof='Rows only when top>159 or top+scale<0; columns only when right<0 or left>255. Every cell primitive lies inside its inclusive outer rectangle. Row index advances by width; seen remains analytical PRE/current-index, including cross-row neighbours. Separate fixed3-pixel hero footer always executes, including scale2 arrow outside an offscreen cell.',
                    originals_before=original_before,commands=commands,
                    binaries=[pin(baseline),pin(OUT/'baseline.o'),pin(OUT/'map_cull_equivalence.exe')],
                    accepted_N20_native_report=pin(accepted),accepted_N20_source=pin(original_source),
                    scope='Only conservative rejection and equivalent coordinate calculation move; no cache, world mutation, line clipping or palette change.')
        assert before==[pin(ROOT/p) for p in sources] and original_before==originals()
        report.write_text(json.dumps(result,indent=2)+'\n','utf8')
        print(json.dumps(dict(host_passed=True,differential=differential)),flush=True)
    else:
        result=json.loads(report.read_text('utf8'))
        assert result['passed'] and result['source_pins']==before
    if args.host_only:return
    # Original checker is immutable. This execution copy changes only its three
    # filesystem expressions; all954 x86 cases/assertions remain byte-identical.
    native_dir=OUT/'native';native_dir.mkdir(exist_ok=True)
    checker=ROOT/'ports/nds/tools/check_map_raster.py';code=checker.read_text('utf8')
    replacements=[("ROOT = Path(__file__).resolve().parents[3]",'ROOT = Path('+repr(ROOT.as_posix())+')'),
                  ("sys.path.insert(0, str(Path(__file__).parent / 'map'))","sys.path.insert(0, str(ROOT / 'ports/nds/tools/map'))"),
                  ("build = ROOT / 'build/nds/n20/host'","build = ROOT / 'build/nds/n21p/map-cull/native'")]
    for a,b in replacements:assert code.count(a)==1;code=code.replace(a,b,1)
    execution=native_dir/'original_checker_execution.py';execution.write_text(code,'utf8')
    native_report=EVIDENCE/'map-cull-native.json'
    command=[sys.executable,'-B','-X','utf8',str(execution),'--out',str(native_report.relative_to(ROOT))]
    log=native_dir/'native-replay.log'
    with log.open('w',encoding='utf8') as stream:
        subprocess.run(command,cwd=ROOT,env=env,stdout=stream,stderr=subprocess.STDOUT,check=True)
    native=json.loads(native_report.read_text('utf8'))
    assert native['passed'] and native['checks']==954 and native['pixels']==39075840
    assert all(r['different_pixels']==0 for r in native['rows'])
    assert original_before==originals() and before==[pin(ROOT/p) for p in sources]
    result.update(native_replay=pin(native_report),native_checker_original=pin(checker),
                  native_checker_execution_copy=pin(execution),native_execution_path_only_replacements=[dict(original=a,replacement=b) for a,b in replacements],
                  native_command=command,native_log=pin(log),originals_after=originals())
    report.write_text(json.dumps(result,indent=2)+'\n','utf8')
    bridge=dict(task='N21P C18 scoped map raster source bridge',passed=True,old_N20_reports_modified=False,
                accepted_N20_native_report=pin(accepted),accepted_N20_source=pin(original_source),
                archived_baseline=pin(old),current_equivalent_source=pin(current),
                equivalence_proof=pin(report),fresh_native_replay=pin(native_report),
                scope='Only four conservative offscreen checks and equivalent coordinate ordering; helper/footer identity and whole-source restoration verified. No global pin bypass.')
    (EVIDENCE/'map-cull-source-supersession.json').write_text(json.dumps(bridge,indent=2)+'\n','utf8')
    print(json.dumps(dict(passed=True,native_checks=954,differential=result['differential'])),flush=True)

if __name__=='__main__':main()
