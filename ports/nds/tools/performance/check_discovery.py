"""Promoted N23 bitset/sort disclosure against frozen C and fresh original instructions."""
from __future__ import annotations
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"map"))
from compiler_cache import compiler_environment
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[4]
OUT=ROOT/'build/nds/n23/discovery-promoted-check'
EVIDENCE=ROOT/'research/evidence/nds/n23'

def pin(path):
    path=Path(path)
    return dict(path=path.relative_to(ROOT).as_posix(),bytes=path.stat().st_size,
                sha256=hashlib.sha256(path.read_bytes()).hexdigest())

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reuse-native',action='store_true',
                        help='reuse a complete fresh native report only after verifying all its pins')
    cli=parser.parse_args()
    OUT.mkdir(parents=True,exist_ok=True)
    EVIDENCE.mkdir(parents=True,exist_ok=True)
    zig=Path(os.environ['LOCALAPPDATA'])/'Programs/Zig/0.13.0/zig.exe'
    env=compiler_environment(ROOT)
    flags=['cc','-DN23_DISCOVERY_CACHE=0','-DN23_DISCOVERY_BITSET=1','-DN23_DISCOVERY_SORT=1','-std=c11','-O2','-Wall','-Wextra','-Werror','-pedantic']
    include=['-Iports/nds/include','-Ibuild/nds/n07/generated']
    common=['ports/nds/render/sprites.c','ports/nds/render/scene.c',
            'ports/nds/render/geometry.c','ports/nds/core/world.c','ports/nds/core/sha256.c']
    sources=['ports/nds/ui/map_discovery.c','ports/nds/include/dok/map_discovery.h',
             'ports/nds/tests/map_discovery_equivalence.c','ports/nds/tests/map_discovery_dump.c',
             'ports/nds/tests/map_discovery_validation.c','ports/nds/tools/map/audit_discovery_clip_edges.py',
             'ports/nds/tests/map_discovery_math.c',
             'ports/nds/tools/performance/check_discovery.py','ports/nds/tools/map/audit_discovery.py']+common
    before=[pin(ROOT/p) for p in sources]
    initial=ROOT/'research/evidence/nds/n21p/initial'
    accepted=json.loads((initial/'discovery-pins.json').read_text('utf8'))
    for row in accepted['source_pins']:
        assert hashlib.sha256((ROOT/row['archived']).read_bytes()).hexdigest()==row['sha256']
    commands=[]
    def build(args):
        command=[str(zig)]+flags+args
        subprocess.run(command,cwd=ROOT,env=env,check=True)
        commands.append(command)
    names=['dok_map_discovery_bounds','dok_map_record_geometry','dok_map_record_sprite',
           'dok_map_discovery_records','dok_map_discovery_update']
    build(['-Iresearch/evidence/nds/n21p/initial/ports/nds/include']+include+
          ['-D'+n+'=baseline_'+n for n in names]+['-c',
           str(initial/'ports/nds/ui/map_discovery.c'),'-o',str(OUT/'baseline.o')])
    precompact=ROOT/'research/evidence/nds/n21p/pre-compact'
    for row in json.loads((precompact/'pins.json').read_text('utf8'))['source_pins']:
        assert hashlib.sha256((ROOT/row['archived']).read_bytes()).hexdigest()==row['sha256']
    build(['-Iresearch/evidence/nds/n21p/pre-compact/ports/nds/include']+include+
          ['-D'+n+'=candidate4_'+n for n in names]+['-c',
           str(precompact/'ports/nds/ui/map_discovery.c'),'-o',str(OUT/'candidate4.o')])
    premath=ROOT/'research/evidence/nds/n21p/pre-math'
    for row in json.loads((premath/'pins.json').read_text('utf8'))['source_pins']:
        assert hashlib.sha256((ROOT/row['archived']).read_bytes()).hexdigest()==row['sha256']
    build(['-Iresearch/evidence/nds/n21p/pre-math/ports/nds/include']+include+
          ['-D'+n+'=candidate5_'+n for n in names]+['-c',
           str(premath/'ports/nds/ui/map_discovery.c'),'-o',str(OUT/'candidate5.o')])
    build(include+['ports/nds/tests/map_discovery_equivalence.c','ports/nds/ui/map_discovery.c']+
          common+[str(OUT/'baseline.o'),str(OUT/'candidate4.o'),str(OUT/'candidate5.o'),'-o',str(OUT/'map_discovery_equivalence.exe')])
    build(include+['ports/nds/tests/map_discovery_validation.c','ports/nds/ui/map_discovery.c']+
          common+[str(OUT/'baseline.o'),str(OUT/'candidate4.o'),str(OUT/'candidate5.o'),'-o',str(OUT/'map_discovery_validation.exe')])
    build(include+['ports/nds/tests/map_discovery_math.c']+common+['-o',str(OUT/'map_discovery_math.exe')])
    math_result=json.loads(subprocess.check_output([str(OUT/'map_discovery_math.exe')],cwd=ROOT,text=True))
    assert math_result['passed'];math_result.update(source_pins=before,binary=pin(OUT/'map_discovery_math.exe'))
    math_report=EVIDENCE/'discovery-math.json'
    math_report.write_text(json.dumps(math_result,indent=2)+'\n','utf8')
    print('actual C math',math_result['actual_C_sar_vectors'],math_result['actual_C_rotation_vectors'],flush=True)
    rows=[];validations=[]
    for level in (1,2,3):
        args=[str(OUT/'map_discovery_equivalence.exe'),
              str(ROOT/f'build/nds/n07/geometry-level{level}.cp866'),
              str(ROOT/f'build/nds/n07/geometry-bindings{level}.bin')]
        row=json.loads(subprocess.check_output(args,cwd=ROOT,text=True))
        row['level']=level;assert row['passed'];rows.append(row)
        print('differential level',level,row,flush=True)
        output=subprocess.check_output([str(OUT/'map_discovery_validation.exe')]+args[1:],cwd=ROOT,text=True)
        validation=json.loads(output.splitlines()[-1]);validation['level']=level
        assert validation['passed'];validations.append(validation)
    validation_report=EVIDENCE/'discovery-validation.json'
    validation_report.write_text(json.dumps(dict(passed=True,rows=validations,
        validation_cases=sum(r['validation_cases'] for r in validations),
        error_cases=sum(r['error_cases'] for r in validations),
        dead_field_vectors=sum(r['dead_field_vectors'] for r in validations),
        source_pins=before,binary=pin(OUT/'map_discovery_validation.exe')),indent=2)+'\n','utf8')
    build(include+['ports/nds/tests/map_discovery_dump.c','ports/nds/ui/map_discovery.c']+
          common+['-o',str(OUT/'map_discovery_dump.exe')])
    build(include+['ports/nds/tests/map_discovery_dump.c','ports/nds/ui/map_discovery.c']+
          common+['-target','x86_64-linux-musl','-static','-o',str(OUT/'map_discovery_dump-linux')])
    native_path=EVIDENCE/'discovery-native.json'
    if cli.reuse_native:
        native=json.loads(native_path.read_text('utf8'))
        assert native['passed']and native['complete_source_gate']and native['source_pins_before_after_equal']
        for item in native['source_pins']+[native['executable']]:assert pin(ROOT/item['path'])==item
    else:
        with (OUT/'native.log').open('w',encoding='utf8')as log:
            subprocess.run([sys.executable,'-B','-X','utf8',str(ROOT/'ports/nds/tools/map/audit_discovery.py'),
                '--dump',str(OUT/'map_discovery_dump.exe'),'--report',str(native_path)],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
        native=json.loads(native_path.read_text('utf8'));assert native['passed']and native['complete_source_gate']
    subprocess.run([sys.executable,'-B','-X','utf8',str(ROOT/'ports/nds/tools/map/audit_discovery_clip_edges.py'),
        '--dump',str(OUT/'map_discovery_dump.exe')],cwd=ROOT,check=True)
    assert before==[pin(ROOT/p) for p in sources]
    report=dict(task='N23',passed=True,rows=rows,validations=validations,math=math_result,source_pins=before,commands=commands,
        native_report=pin(native_path),linux_static_link=pin(OUT/'map_discovery_dump-linux'),
        scope='Actual promoted cache0/bitset1/sort1 C: full records/order/stats/visited/errors plus fresh complete original-instruction oracle; Linux linked, not executed')
    (EVIDENCE/'discovery-check.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    print(json.dumps(dict(passed=True,cases=sum(r['cases']for r in rows))))
if __name__=='__main__':main()
