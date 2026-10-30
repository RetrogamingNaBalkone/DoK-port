"""N21P C7 redundant append-clear removal: complete cell byte equivalence."""
from __future__ import annotations
from compiler_cache import compiler_environment
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[4]
OUT=ROOT/'build/nds/n21p/geometry-clear'
EVIDENCE=ROOT/'research/evidence/nds/n21p'

def pin(path):
    path=Path(path)
    return dict(path=path.relative_to(ROOT).as_posix(),bytes=path.stat().st_size,
                sha256=hashlib.sha256(path.read_bytes()).hexdigest())

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    archive=EVIDENCE/'pre-geometry/pins.json'
    row=json.loads(archive.read_text('utf8'))['source_pins'][0]
    old=ROOT/row['archived'];current=ROOT/row['path']
    assert pin(old)['sha256']==row['sha256']
    old_text=old.read_text('utf8');new_text=current.read_text('utf8')
    removed='    memset(quad,0,sizeof(*quad));\n'
    assert old_text.count(removed)==1 and new_text==old_text.replace(removed,'',1)
    # append is translation-unit local; the full candidate clear precedes all
    # builder call sites. Monotonic count allocates each already-zeroed slot once.
    assert 'static dok_geometry_quad *append(' in new_text
    assert new_text.count('append(')==6
    clear=new_text.index('memset(candidate,0,sizeof(*candidate));')
    function=new_text.index('dok_geometry_status dok_geometry_build_cell(')
    assert function<clear
    assert all(p>clear for p in range(function,len(new_text)) if new_text.startswith('append(',p))
    sources=['ports/nds/render/geometry.c','ports/nds/include/dok/geometry.h',
             'ports/nds/core/world.c','ports/nds/core/sha256.c','ports/nds/include/dok/world.h',
             'ports/nds/tests/geometry_clear_equivalence.c','ports/nds/tools/map/check_geometry_clear.py']
    before=[pin(ROOT/p) for p in sources]
    env=compiler_environment(ROOT)
    zig=Path(os.environ['LOCALAPPDATA'])/'Programs/Zig/0.13.0/zig.exe'
    common=[str(zig),'cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-pedantic','-Iports/nds/include']
    commands=[common+['-Ddok_geometry_build_cell=baseline_dok_geometry_build_cell','-c',str(old),'-o',str(OUT/'baseline.o')],
              common+['ports/nds/tests/geometry_clear_equivalence.c','ports/nds/render/geometry.c',
                      'ports/nds/core/world.c','ports/nds/core/sha256.c',str(OUT/'baseline.o'),
                      '-o',str(OUT/'geometry_clear_equivalence.exe')]]
    for command in commands:subprocess.run(command,cwd=ROOT,env=env,check=True)
    rows=[];fixtures=[]
    for level in (1,2,3):
        fixture=[ROOT/f'build/nds/n07/geometry-level{level}.cp866',ROOT/f'build/nds/n07/geometry-bindings{level}.bin']
        fixtures.extend(pin(p) for p in fixture)
        result=json.loads(subprocess.check_output([str(OUT/'geometry_clear_equivalence.exe')]+list(map(str,fixture)),cwd=ROOT,text=True))
        assert result['passed'];result['level']=level;rows.append(result)
        print('geometry clear level',level,result,flush=True)
    assert before==[pin(ROOT/p) for p in sources]
    result=dict(task='N21P C7 redundant append clear',passed=True,source_pins=before,
                source_pins_before_after_equal=True,archived_source=pin(old),archive_manifest=pin(archive),
                proof='Only the static append memset was removed. Full candidate memset precedes all append call sites; count allocates each zero slot once.',
                changed_lines=1,changed_port='NDS only',native_constructor_replay_needed=False,
                cells=sum(r['cells'] for r in rows),base_sentinel_cases=sum(r['base_sentinel_cases'] for r in rows),
                mutated_binding_cases=sum(r['mutated_binding_cases'] for r in rows),
                error_returns=sum(r['error_returns'] for r in rows),rows=rows,fixtures=fixtures,commands=commands,
                binary=pin(OUT/'geometry_clear_equivalence.exe'),
                scope='Actual old/new geometry builder status, entire candidate and out, including unchanged out on error; no scene, UV, endpoints or constructor behavior changed')
    target=EVIDENCE/'geometry-clear-equivalence.json'
    target.write_text(json.dumps(result,indent=2)+'\n','utf8')
    affected=[]
    for name in ('discovery-equivalence.json','discovery-native.json','discovery-validation.json','discovery-math.json'):
        path=EVIDENCE/name;report=json.loads(path.read_text('utf8'))
        matching=[p for p in report['source_pins'] if p['path']==row['path']]
        assert len(matching)==1 and matching[0]['sha256']==row['sha256']
        affected.append(dict(report=pin(path),original_compiled_path=row['path'],original_sha256=row['sha256']))
    bridge=dict(task='N21P C7 geometry source-pin bridge',passed=True,
                scope='Only ports/nds/render/geometry.c; exactly one redundant append memset removed. No other source pins are substituted.',
                original_compiled_path=row['path'],archived_original=pin(old),
                current_equivalent_source=pin(current),equivalence_proof=pin(target),affected_reports=affected,
                reason='Static append has five call sites, all after the full1108-byte candidate memset. Monotonic count selects fresh zero slots at most8 times. Actual entire candidate/out/status byte equivalence covers every cell and binding errors.',
                native_replay=False,old_native_reports_modified=False)
    (EVIDENCE/'geometry-source-supersession.json').write_text(json.dumps(bridge,indent=2)+'\n','utf8')
    print(json.dumps({k:result[k] for k in ('passed','cells','base_sentinel_cases','mutated_binding_cases','error_returns')}))

if __name__=='__main__':main()
