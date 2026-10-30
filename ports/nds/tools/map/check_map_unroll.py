"""C12 exact tiny rectangle byte stores, differential plus original954 replay."""
from __future__ import annotations
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
from check_scene_merge import function

ROOT=Path(__file__).resolve().parents[4]
OUT=ROOT/'build/nds/n21p/map-unroll'
EVIDENCE=ROOT/'research/evidence/nds/n21p'
def pin(path):
    path=Path(path)
    return dict(path=path.relative_to(ROOT).as_posix(),bytes=path.stat().st_size,
                sha256=hashlib.sha256(path.read_bytes()).hexdigest())
def originals():
    result={}
    for language in ('ru','en'):
        manifest=json.loads((ROOT/f'research/evidence/automap/{language}/manifest.json').read_text('utf8'))
        result[language]=pin(ROOT/manifest['sample'])
        assert result[language]['sha256']==manifest['sha256_before']==manifest['sha256_after']
    return result
def main():
    OUT.mkdir(parents=True,exist_ok=True)
    archive=EVIDENCE/'pre-map-unroll/pins.json';saved=json.loads(archive.read_text('utf8'))['source_pins']
    for p in saved:assert pin(ROOT/p['archived'])['sha256']==p['sha256']
    old=ROOT/next(p['archived'] for p in saved if p['path']=='ports/nds/ui/map_raster.c')
    current=ROOT/'ports/nds/ui/map_raster.c';old_text=old.read_text('utf8');new_text=current.read_text('utf8')
    names=('pixel','native_class','seen','blocks_line','dok_map_raster')
    for name in names:assert function(old_text,name)==function(new_text,name),name
    old_rect=function(old_text,'rect');new_rect=function(new_text,'rect')
    assert old_rect[:old_rect.index('    for (y =')]==new_rect[:new_rect.index('    for (y =')]
    sources=['ports/nds/ui/map_raster.c','ports/nds/include/dok/map_raster.h',
             'ports/nds/tests/map_unroll_equivalence.c','ports/nds/tools/map/check_map_unroll.py',
             'ports/nds/tools/check_map_raster.py','ports/nds/tools/map/original_map.py']
    before=[pin(ROOT/p) for p in sources];original_before=originals()
    env=os.environ.copy();env['ZIG_LOCAL_CACHE_DIR']=str(OUT/'zig-local-cache');env['ZIG_GLOBAL_CACHE_DIR']=str(OUT/'zig-global-cache')
    zig=Path(os.environ['LOCALAPPDATA'])/'Programs/Zig/0.13.0/zig.exe'
    flags=[str(zig),'cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-pedantic','-Iports/nds/include']
    baseline=OUT/'baseline_raster.c'
    baseline.write_bytes(old.read_bytes()+b'\nvoid baseline_rect(uint8_t *out,int32_t x1,int32_t y1,int32_t x2,int32_t y2,uint8_t c){rect(out,x1,y1,x2,y2,c);}\n')
    commands=[flags+['-Ddok_map_raster=baseline_dok_map_raster','-c',str(baseline),'-o',str(OUT/'baseline.o')],
              flags+['ports/nds/tests/map_unroll_equivalence.c',str(OUT/'baseline.o'),'-o',str(OUT/'map_unroll_equivalence.exe')]]
    for command in commands:subprocess.run(command,cwd=ROOT,env=env,check=True)
    differential=json.loads(subprocess.check_output([str(OUT/'map_unroll_equivalence.exe')],cwd=ROOT,text=True))
    assert differential['passed'];print('host differential',differential,flush=True)
    # Keep original N20 checker and accepted evidence immutable. The execution
    # copy changes exactly three filesystem expressions; all oracle cases,
    # x86 execution and pixel assertions remain the original checker logic.
    native_dir=OUT/'native';native_dir.mkdir(exist_ok=True)
    checker=ROOT/'ports/nds/tools/check_map_raster.py';code=checker.read_text('utf8')
    replacements=[("ROOT = Path(__file__).resolve().parents[3]",'ROOT = Path('+repr(ROOT.as_posix())+')'),
                  ("sys.path.insert(0, str(Path(__file__).parent / 'map'))","sys.path.insert(0, str(ROOT / 'ports/nds/tools/map'))"),
                  ("build = ROOT / 'build/nds/n20/host'","build = ROOT / 'build/nds/n21p/map-unroll/native'")]
    for original,replacement in replacements:
        assert code.count(original)==1;code=code.replace(original,replacement,1)
    execution=native_dir/'original_checker_execution.py';execution.write_text(code,'utf8')
    native_report=EVIDENCE/'map-unroll-native.json'
    native_command=[sys.executable,'-B','-X','utf8',str(execution),'--out',str(native_report.relative_to(ROOT))]
    log=native_dir/'native-replay.log'
    with log.open('w',encoding='utf8') as stream:
        subprocess.run(native_command,cwd=ROOT,env=env,stdout=stream,stderr=subprocess.STDOUT,check=True)
    native=json.loads(native_report.read_text('utf8'));assert native['passed'] and native['checks']==954 and native['pixels']==39075840
    for row in native['rows']:assert row['different_pixels']==0
    original_after=originals();assert original_before==original_after
    assert before==[pin(ROOT/p) for p in sources]
    accepted=ROOT/'research/evidence/nds/n20/raster-check.json';n20=json.loads(accepted.read_text('utf8'))
    assert n20['passed'] and n20['checks']==954 and pin(old)['sha256']==n20['source_sha256']['ports/nds/ui/map_raster.c']
    for row in n20['rows']:
        assert row['different_pixels']==0
    snapshot=EVIDENCE/'c12-map/ports/nds/ui/map_raster.c';snapshot.parent.mkdir(parents=True,exist_ok=True);snapshot.write_bytes(current.read_bytes())
    result=dict(task='N21P C12 exact tiny rectangle stores',passed=True,source_pins=before,source_pins_before_after_equal=True,
                archived_original=pin(old),current_snapshot=pin(snapshot),differential=differential,
                unchanged_function_body_sha256={n:hashlib.sha256(function(new_text,n).encode('utf8')).hexdigest() for n in names},
                clipping_and_inclusive_end_prefix_unchanged=True,rectangle_width_bound=9,
                width_proof='At scales2/3/4/6/8: FC/horizontal-wall inclusive width=scale+1<=9, marker width=scale-1<=7, horizontal-door width=scale<=8, vertical width1. Clipping cannot increase width. General width>9 uses original memset fallback.',
                strict_aliasing='Only uint8 stores; no typed word cast or output alignment assumption',
                native_replay=pin(native_report),native_checker_original=pin(checker),native_checker_execution_copy=pin(execution),
                native_execution_path_only_replacements=[dict(original=a,replacement=b) for a,b in replacements],
                native_command=native_command,native_log=pin(log),accepted_N20_native_report=pin(accepted),
                originals_before=original_before,originals_after=original_after,commands=commands,
                binaries=[pin(OUT/'baseline_raster.c'),pin(OUT/'baseline.o'),pin(OUT/'map_unroll_equivalence.exe'),pin(native_dir/'map-raster.dll')],
                scope='Only per-row tiny memset implementation changed; every cell/pass/seen/PRE/arrow operation remains byte-identical source. Differential covers random full glyph planes, visited, headings/scales/offsets, all4 byte alignments, guard tails, reversed/clipped/general rectangles and invalid API inputs. Fresh954 original PKR/PKE pixel comparisons passed; accepted N20 evidence not overwritten.')
    report=EVIDENCE/'map-unroll-equivalence.json';report.write_text(json.dumps(result,indent=2)+'\n','utf8')
    bridge=dict(task='N21P C12 raster source bridge',passed=True,original_compiled_path='ports/nds/ui/map_raster.c',
                archived_original=pin(old),current_equivalent_source=pin(current),equivalence_proof=pin(report),
                accepted_N20_native_report=pin(accepted),fresh_native_replay=pin(native_report),old_N20_reports_modified=False,
                scope='Only bounded per-row byte stores replace tiny memset; original native semantics freshly replayed with unchanged case logic.')
    (EVIDENCE/'map-unroll-source-supersession.json').write_text(json.dumps(bridge,indent=2)+'\n','utf8')
    print(json.dumps(dict(passed=True,differential=differential,native_checks=954)))
if __name__=='__main__':main()
