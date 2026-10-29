"""N13P immutable baseline, accumulated packet and paired ARM9 builds/runs."""
from __future__ import annotations
import argparse,json,hashlib,struct,shutil,subprocess,sys,os
from pathlib import Path
import check_effects as base
ROOT=base.ROOT;OUT=ROOT/'build/nds/n13p';EVIDENCE=ROOT/'research/evidence/nds/n13p'
MUTABLE=['ports/nds/platform/effects_probe/main.c','ports/nds/platform/effects_probe/effects_prepare.inc',
         'ports/nds/Makefile.n13','ports/nds/platform/render_probe/render_backend.c']
def load(p):return json.loads(Path(p).read_text('utf8'))
def prepare():
    accepted=load(ROOT/'analysis/nds-n13-effects.json');assert accepted['passed'] and accepted['status']=='ready-emulator'
    assert load(ROOT/'analysis/nds-n12-sprites.json')['passed'] and load(ROOT/'analysis/nds-render-performance.json')['passed']
    OUT.mkdir(parents=True,exist_ok=True);EVIDENCE.mkdir(parents=True,exist_ok=True)
    proof=EVIDENCE/'baseline-inputs.json'
    if not proof.exists():
        archive=EVIDENCE/'baseline-sources';rows=[]
        for rel in MUTABLE:
            p=ROOT/rel;dest=archive/rel;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,dest)
            rows.append(dict(original=base.artifact(p),archive=base.artifact(dest)))
        (EVIDENCE/'source-supersession.json').write_text(json.dumps(rows,indent=2)+'\n','utf8')
        current=load(ROOT/'research/evidence/nds/n13/final/ru/build-manifest.json')
        for row in current['sources']:
            if row['path'].endswith(('.c','.h','.inc')):assert base.digest(ROOT/row['path'])==row['sha256'],row['path']
        for row in accepted['artifacts']:assert base.digest(ROOT/row['path'])==row['sha256']
        proof.write_text(json.dumps(dict(task='N13P',accepted_n13=base.artifact(ROOT/'analysis/nds-n13-effects.json'),
            accepted_n12=base.artifact(ROOT/'analysis/nds-n12-sprites.json'),accepted_n07p=base.artifact(ROOT/'analysis/nds-render-performance.json'),
            source_supersession=base.artifact(EVIDENCE/'source-supersession.json'),sources=current['sources'],artifacts=accepted['artifacts']),indent=2)+'\n','utf8')
    else:
        pinned=load(proof);archives=load(EVIDENCE/'source-supersession.json')
        assert base.digest(EVIDENCE/'source-supersession.json')==pinned['source_supersession']['sha256']
        if (EVIDENCE/'source-supersession-extra.json').exists():archives+=load(EVIDENCE/'source-supersession-extra.json')
        for row in pinned['sources']:
            if not row['path'].endswith(('.c','.h','.inc')):continue
            if base.digest(ROOT/row['path'])!=row['sha256']:
                old=next(r for r in archives if r['original']['path']==row['path'])
                assert row['sha256']==old['original']['sha256']==base.digest(ROOT/old['archive']['path'])
    old=(ROOT/'build/nds/n13/scene.dat').read_bytes();host=load(ROOT/'research/evidence/nds/n13/host-check.json')
    assert hashlib.sha256(old).hexdigest()==host['fixture']['packet_sha256']
    n=struct.unpack_from('<I',old,12)[0];assert n==37
    records=[old[64+i*64:128+i*64] for i in range(n)]
    for level,scan in enumerate(host['levels'],1):
        x,z,h=scan['heavy'];records.append(struct.pack('<16I',len(records)+1,level,x,(-300)&0xffffffff,z,h,0,0,0,90,0,20,x//480,z//480,0,0))
    records.append(struct.pack('<16I',len(records)+1,2,2160,(-300)&0xffffffff,2160,0,0,0,3,120,2,21,4,4,0,0))
    body=b''.join(records)+old[64+n*64:]
    data=b'N07SCN1\0'+struct.pack('<6I',1,len(records),64,10240,64+len(body),0)+hashlib.sha256(body).digest()+body
    (OUT/'scene.dat').write_bytes(data)
    fixture=dict(task='N13P',cases=len(records),presentations=sum(struct.unpack('<16I',r)[9] for r in records),
        artifact=base.artifact(OUT/'scene.dat'),inherited_cases=37,new_cases='3 maximum-LIGHT stationary poses90frames each; one native water turn120frames',
        source_clock='N08 scheduler driven by explicit166tick fixture per service sweep; real-clock integration was tested in N13')
    (EVIDENCE/'fixture.json').write_text(json.dumps(fixture,indent=2)+'\n','utf8');return fixture
def build(label):
    if label.startswith('baseline') and (EVIDENCE/'baseline-inputs.json').exists():
        pinned=load(EVIDENCE/'baseline-inputs.json')
        changed=any(base.digest(ROOT/r['path'])!=r['sha256'] for r in pinned['sources'] if r['path'].endswith(('.c','.h','.inc')))
        if changed:
            target=OUT/label
            if not (target/'build-manifest.json').exists():
                raise RuntimeError('Frozen baseline missing; restore its archived sources before rebuilding, never label current optimized sources as baseline')
            manifest=load(target/'build-manifest.json')
            for row in manifest['artifacts']:assert base.digest(ROOT/row['path'])==row['sha256']
            language='en' if label.endswith('en') else 'ru'
            expected=next(r for r in pinned['artifacts'] if r['path']==f'build/nds/n13/{language}/dok_nds_effects.nds')
            assert base.digest(target/'dok_nds_effects.nds')==expected['sha256']
            return target
    original=base.OUT
    try:
        base.OUT=OUT
        return base.build(label,2 if label.endswith('en') else 1,label.endswith('audit'),light_cache=label.startswith('optimized'),gpu_fog=label.startswith('fog'))
    finally:base.OUT=original
def runtime(label):
    target=OUT/label
    command=[sys.executable,'-B','-X','utf8',ROOT/'ports/nds/tools/run_effects_emulator.py','--target',target,'--packet',OUT/'scene.dat']
    with (target/'runtime.log').open('w',encoding='utf8') as stream:r=subprocess.run(list(map(str,command)),stdout=stream,stderr=subprocess.STDOUT)
    print(f'N13P {label}: exit {r.returncode}',flush=True);return r.returncode
def host():
    env=dict(os.environ,ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-global-cache'),ZIG_LOCAL_CACHE_DIR=str(ROOT/'build/zig-local-cache'))
    zig=Path.home()/'AppData/Local/Programs/Zig/0.13.0/zig.exe'
    report=dict(passed=False,units={},commands=[])
    for name in ('test_effects','test_effects_runtime','test_effect_cache_replacement','test_light_cache'):
        extra=['ports/nds/platform/effects_probe/effects_runtime.c'] if name.endswith('runtime') else ['ports/nds/render/light_cache.c'] if name=='test_light_cache' else []
        cmd=[zig,'cc',*base.FLAGS,*base.CORE,*base.SOURCES,'ports/nds/render/effects.c','ports/nds/render/effect_cache.c',*extra,f'ports/nds/tests/{name}.c','-o',OUT/(name+'.exe')]
        base.run(cmd,env=env);report['commands'].append(list(map(str,cmd)))
        args=[OUT/(name+'.exe')]+([ROOT/'build/nds/assets/dok-assets.ndspak'] if name in ('test_effects_runtime','test_light_cache') else [])
        value=json.loads(base.run(args));assert not value.get('failures',0);report['units'][name]=value
    report['native']=[base.artifact(ROOT/'research/evidence/nds/n13'/n) for n in ('original-report.json','actual-assets-report.json')]
    report['passed']=True;(EVIDENCE/'host-check.json').write_text(json.dumps(report,indent=2)+'\n','utf8');return report['units']
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--prepare',action='store_true')
    p.add_argument('--target',choices=('baseline-ru','baseline-en','optimized-ru','optimized-en','optimized-audit','fog-ru','fog-en','fog-audit'))
    p.add_argument('--runtime',action='store_true');p.add_argument('--skip-build',action='store_true');p.add_argument('--host',action='store_true');args=p.parse_args()
    if args.prepare:print(json.dumps(prepare(),indent=2))
    if args.host:print(json.dumps(host(),indent=2))
    if args.target:
        if not args.skip_build:build(args.target)
        if args.runtime:return runtime(args.target)
    return 0
if __name__=='__main__':raise SystemExit(main())
