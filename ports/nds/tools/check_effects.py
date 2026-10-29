"""N13 strict C/native source/ARM9 gates; previous accepted inputs are preserved."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from check_interactions import CORE,FLAGS
ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'build/nds/n13'
EVIDENCE=ROOT/'research/evidence/nds/n13'
SOURCES=[f'ports/nds/render/{n}.c' for n in ('sprites','sprite_assets','texture_cache')]
def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def artifact(path):return dict(path=Path(path).relative_to(ROOT).as_posix(),bytes=Path(path).stat().st_size,sha256=digest(path))
def run(argv,**kwargs):
    result=subprocess.run(list(map(str,argv)),cwd=ROOT,capture_output=True,text=True,encoding='utf8',**kwargs)
    if result.returncode:raise RuntimeError(result.stdout+result.stderr)
    return result.stdout
def build(variant,language=1,audit=False,legacy=False,real_clock=False,water_phases=4294967295,light_cache=False,gpu_fog=False):
    target=OUT/variant;target.mkdir(parents=True,exist_ok=True);(target/'generated').mkdir(exist_ok=True)
    shutil.copyfile(ROOT/'build/nds/n07/generated/render_trig.inc',target/'generated/render_trig.inc')
    msys=Path.home()/'.local/msys64';env=dict(os.environ,MSYSTEM='UCRT64',CHERE_INVOKING='1',MSYS2_PATH_TYPE='minimal')
    prefix='set -eo pipefail\nsource /opt/wonderful/bin/wf-env\n'
    def shell(script):return run([msys/'usr/bin/bash.exe','--login','-s'],input=prefix+script+'\n',env=env)
    packages=dict(line.split() for line in shell('wf-pacman -Q').splitlines())
    lock=json.loads((ROOT/'ports/nds/toolchain_versions.json').read_text('utf8'))
    assert all(packages.get(k)==v for k,v in lock['packages'].items())
    repo=ROOT.as_posix();repo='/'+repo[0].lower()+repo[2:];quote="'"+repo.replace("'","'\\''")+"'"
    makefile='Makefile.n12' if legacy else 'Makefile.n13'
    name='dok_nds_sprites' if legacy else 'dok_nds_effects'
    relative=target.relative_to(ROOT).as_posix()
    log=shell(f'cd {quote}\nmake -C ports/nds -f {makefile} OUTDIR=../../{relative} SPRITE_LANGUAGE={language} SPRITE_AUDIT={int(audit)} EFFECT_REAL_CLOCK={int(real_clock)} EFFECT_WATER_PHASES={water_phases}u EFFECT_LIGHT_CACHE={int(light_cache)} EFFECT_GPU_FOG={int(gpu_fog)} all')
    (target/'build.log').write_text(log,'utf8')
    manifest=dict(packages=packages,language=language,audit=audit,
        sources=[artifact(p) for p in sorted((ROOT/'ports/nds').rglob('*')) if p.is_file() and p.suffix in ('.c','.h','.py','.inc')],
        artifacts=[artifact(target/f'{name}.{ext}') for ext in ('nds','elf','map')])
    (target/'build-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n','utf8')
    return target

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--skip-host',action='store_true');parser.add_argument('--skip-build',action='store_true')
    parser.add_argument('--variant',choices=('ru','en','audit','real'),default='ru');parser.add_argument('--runtime',action='store_true')
    args=parser.parse_args();OUT.mkdir(parents=True,exist_ok=True);EVIDENCE.mkdir(parents=True,exist_ok=True)
    gate=dict(task='N13',passed=False,commands=[],previous_pins=0)
    try:
        previous=json.loads((ROOT/'research/evidence/nds/n12/host-ru.json').read_text('utf8'))
        proof=json.loads((EVIDENCE/'baseline/source-supersession.json').read_text('utf8'))
        for row in previous['sources']:
            if digest(ROOT/row['path'])!=row['sha256']:
                archived=next(r for r in proof if r['path']==row['path'])
                assert row['sha256']==archived['sha256']==digest(ROOT/archived['archive']),row['path']
            gate['previous_pins']+=1
        if not args.skip_host:
            env=dict(os.environ,ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-global-cache'),ZIG_LOCAL_CACHE_DIR=str(ROOT/'build/zig-local-cache'))
            zig=Path.home()/'AppData/Local/Programs/Zig/0.13.0/zig.exe'
            modules=CORE+SOURCES+['ports/nds/render/effects.c','ports/nds/render/effect_cache.c']
            gate['units']={}
            for name in ('test_effects','test_effects_runtime','effects_dump','effects_scene_dump','test_sprites','test_door_render','test_render_scene','render_dump'):
                extra=['ports/nds/platform/effects_probe/effects_runtime.c'] if name=='test_effects_runtime' else []
                if name=='test_door_render':extra+=['ports/nds/tests/door_render_cases.c']
                command=[zig,'cc',*FLAGS,*modules,*extra,f'ports/nds/tests/{name}.c','-o',OUT/(name+'.exe')]
                run(command,env=env);gate['commands'].append(list(map(str,command)))
                if name.startswith('test_'):
                    argv=[OUT/(name+'.exe')]+([ROOT/'build/nds/assets/dok-assets.ndspak'] if name=='test_effects_runtime' else [])
                    gate['units'][name]=json.loads(run(argv));assert not gate['units'][name].get('failures',0)
            gate['levels']=[]
            for level in (1,2,3):
                gate['levels'].append(json.loads(run([OUT/'effects_scene_dump.exe',ROOT/'build/nds/assets/dok-assets.ndspak',level,'--scan',1])))
            (EVIDENCE/'host-check.json').write_text(json.dumps(gate,indent=2)+'\n','utf8')
            gate['metrics_tests']=run([sys.executable,'-B','-X','utf8',ROOT/'ports/nds/tests/test_render_metrics.py']).strip()
            for script in ('audit_effects.py','audit_effect_assets.py','audit_light_geometry.py'):
                command=[sys.executable,'-B','-X','utf8',ROOT/'ports/nds/tools'/script,'--dump',OUT/('effects_scene_dump.exe' if script=='audit_light_geometry.py' else 'effects_dump.exe')]
                print(run(command),flush=True);gate['commands'].append(list(map(str,command)))
            gate['registry_tests']=run([sys.executable,'-B','-X','utf8',ROOT/'ports/nds/tools/audit_effect_registry.py']).strip()
            from prepare_effects_fixture import prepare
            gate['fixture']=prepare()
            (EVIDENCE/'host-check.json').write_text(json.dumps(gate|dict(passed=True),indent=2)+'\n','utf8')
        if not args.skip_build:build(args.variant,2 if args.variant=='en' else 1,args.variant=='audit',real_clock=args.variant=='real')
        gate['passed']=True
    finally:
        gate['sources']=[artifact(p) for p in sorted((ROOT/'ports/nds').rglob('*')) if p.is_file() and p.suffix in ('.c','.h','.py','.inc')]
        (EVIDENCE/f'gate-{args.variant}.json').write_text(json.dumps(gate,indent=2)+'\n','utf8')
    if args.runtime:
        target=OUT/args.variant
        command=[sys.executable,'-B','-X','utf8',ROOT/'ports/nds/tools/run_effects_emulator.py','--target',target,'--packet',OUT/'scene.dat']
        with (target/'runtime.log').open('w',encoding='utf8') as stream:
            result=subprocess.run(list(map(str,command)),stdout=stream,stderr=subprocess.STDOUT)
        print(f'N13 {args.variant} runtime exit {result.returncode}',flush=True);return result.returncode
    print('N13 gates passed');return 0
if __name__=='__main__':raise SystemExit(main())
