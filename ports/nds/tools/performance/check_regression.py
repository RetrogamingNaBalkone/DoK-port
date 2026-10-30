"""Fresh promoted N23 collision/AI/combat regression; original evidence is read-only."""
import hashlib,json,os,random,sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'ports/nds/tools'))
from check_items import artifact,run
from check_combat import SOURCES
from check_interactions import FLAGS
import audit_collision
import audit_ai_combat

OUT=ROOT/'build/nds/n23/regression'
E=ROOT/'research/evidence/nds/n23/regression'

def collision_destination(path):
    path=Path(path).absolute();expected=E/'original-collision.json'
    assert path==expected and path.resolve()==path
    assert not any(p.is_symlink() or p.is_junction() for p in (path,*path.parents) if p.exists())
    path.parent.mkdir(parents=True,exist_ok=True);return path

def native_ai():
    before=audit_ai_combat.originals();model=audit_ai_combat.models();fields=audit_ai_combat.trace_fields()
    rows=[dict(zip(fields,json.loads(line),strict=True)) for line in (E/'callbacks.jsonl').read_text('utf8').splitlines()]
    rng=random.Random(171029)
    vectors=[(0,t,d,0,0) for t in (0,1,39999,40000,999999,1000000,0xffffffff) for d in (-32768,-1,0,3,99,32767)]
    vectors += [(1,rng.randint(-32768,32767),rng.randint(-32768,32767),0,0) for _ in range(2000)]
    vectors += [(1,0,d,0,0) for d in (-32768,-1,0,1,32767)]
    stdout=run([OUT/'ai_combat_dump.exe'],input=''.join(' '.join(map(str,v))+'\n' for v in vectors))
    expected=list(map(int,stdout.split()));assert len(expected)==len(vectors)
    report=dict(task='N23',passed=False,callback_records=len(rows),languages={},whole_DOS_session=False)
    for language in (1,2):
        native=audit_ai_combat.Original(language);count=0
        for row in rows:
            if row['language']==language:
                native.callback(row,model[language,row['level'],row['glyph']]);count+=1
        for v,value in zip(vectors,expected):
            assert (native.due_word(v[1],v[2]) if v[0]==0 else native.damage(v[1],v[2]))==value
        scripts=[(['self','self','hero'],1,[0,0,0]),(['pass8','pass16','hero'],1,[0,1,0]),(['block'],0,[0]),(['pass8','block'],0,[0,1]),(['hero'],1,[0]),(['self','block'],0,[0,0]),(['self'],0,[0]*5000)]
        for script,visible,planes in scripts:assert native.sight_query(script)==(visible,planes)
        report['languages'][str(language)]=dict(callbacks=count,burst_thresholds=42,damage_words=2005,sight_scripts=7)
    assert before==audit_ai_combat.originals()
    report.update(passed=True,original_sha256_before=before,original_sha256_after=audit_ai_combat.originals(),
        probe=artifact(OUT/'ai_combat_dump.exe'),transcript=artifact(E/'callbacks.jsonl'),
        source_oracle=artifact(ROOT/'ports/nds/tools/audit_ai_combat.py'),
        scope='Original N17 callback instructions with the unchanged controlled service boundaries; current N22 host transcript')
    (E/'original-ai-combat.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    return report

def main():
    OUT.mkdir(parents=True,exist_ok=True);E.mkdir(parents=True,exist_ok=True)
    env=dict(os.environ,ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-global-cache'),ZIG_LOCAL_CACHE_DIR=str(OUT/'zig-local-cache'))
    zig=Path.home()/'AppData/Local/Programs/Zig/0.13.0/zig.exe'
    tests={};commands=[]
    cases={'test_collision':['collision_cases.c'],'test_collision_routes':['collision_routes.c'],
           'test_ai':['ai_cases.c'],'test_ai_combat':['ai_combat_cases.c'],'test_combat':['combat_cases.c'],
           'test_sprites':[],'ai_combat_dump':[],'collision_dump':[]}
    for name,shared in cases.items():
        cmd=[zig,'cc',*FLAGS,'-DN23_COMBAT_SAR=1','-DN23_COLLISION_OBJECT=1',
             '-DN23_NATIVE_DIV480=1','-DN23_COLLISION_CORNERS=1','-DN23_CLOCK_FAST32=1',
             *SOURCES,'ports/nds/core/ai.c',*[f'ports/nds/tests/{p}' for p in shared],f'ports/nds/tests/{name}.c','-o',OUT/(name+'.exe')]
        run(cmd,env=env);commands.append(list(map(str,cmd)))
        if name.startswith('test_'):
            args=[OUT/(name+'.exe')]
            if name not in ('test_collision','test_sprites'):args.append(ROOT/'build/nds/assets/dok-assets.ndspak')
            if name=='test_ai_combat':args.append(E/'callbacks.jsonl')
            tests[name]=json.loads(run(args));assert not tests[name].get('failures',0)
            print(name,json.dumps(tests[name]),flush=True)
    old_argv=sys.argv
    try:
        audit_collision.destination=collision_destination
        sys.argv=['audit_collision.py','--probe',str(OUT/'collision_dump.exe'),'--output',str(E/'original-collision.json')]
        audit_collision.main()
    finally:sys.argv=old_argv
    collision=json.loads((E/'original-collision.json').read_text('utf8'));assert collision['status']=='passed'
    native=native_ai()
    report=dict(task='N23',passed=True,tests=tests,commands=commands,native_ai=native,
                collision_oracle=artifact(E/'original-collision.json'),
                sources=[artifact(p) for p in sorted((ROOT/'ports/nds').rglob('*')) if p.is_file() and p.suffix in ('.c','.h','.inc','.py')],
                compiler=run([zig,'version']).strip(),scope='Fresh Windows host and original-instruction regression; no historical task outputs overwritten')
    (E/'check.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    print(json.dumps(dict(passed=True,tests=len(tests),native_callbacks=native['callback_records'])))

if __name__=='__main__':main()
