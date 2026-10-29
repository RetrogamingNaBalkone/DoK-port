"""Reproducible N12 host/original/ARM9 gates, independently of GBA/SDL2."""
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
OUT=ROOT/'build/nds/n12'
EVIDENCE=ROOT/'research/evidence/nds/n12'
SOURCES=[f'ports/nds/render/{n}.c' for n in ('sprites','sprite_assets','texture_cache')]
def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def artifact(path):return dict(path=Path(path).relative_to(ROOT).as_posix(),bytes=Path(path).stat().st_size,sha256=digest(path))
def run(argv,**kwargs):
    result=subprocess.run(list(map(str,argv)),cwd=ROOT,capture_output=True,text=True,encoding='utf8',**kwargs)
    if result.returncode:raise RuntimeError(result.stdout+result.stderr)
    return result.stdout
def build(variant,language=1,audit=False):
    target=OUT/variant;target.mkdir(parents=True,exist_ok=True);(target/'generated').mkdir(exist_ok=True)
    shutil.copyfile(ROOT/'build/nds/n07/generated/render_trig.inc',target/'generated/render_trig.inc')
    msys=Path.home()/'.local/msys64';env=dict(os.environ,MSYSTEM='UCRT64',CHERE_INVOKING='1',MSYS2_PATH_TYPE='minimal')
    prefix='set -eo pipefail\nsource /opt/wonderful/bin/wf-env\n'
    def shell(script):return run([msys/'usr/bin/bash.exe','--login','-s'],input=prefix+script+'\n',env=env)
    packages=dict(line.split() for line in shell('wf-pacman -Q').splitlines())
    lock=json.loads((ROOT/'ports/nds/toolchain_versions.json').read_text('utf8'))
    assert all(packages.get(k)==v for k,v in lock['packages'].items())
    repo=ROOT.as_posix();repo='/'+repo[0].lower()+repo[2:];quote="'"+repo.replace("'","'\\''")+"'"
    log=shell(f'cd {quote}\nmake -C ports/nds -f Makefile.n12 OUTDIR=../../build/nds/n12/{variant} SPRITE_LANGUAGE={language} SPRITE_AUDIT={int(audit)} all')
    (target/'build.log').write_text(log,'utf8')
    manifest=dict(packages=packages,language=language,audit=audit,
        sources=[artifact(p) for p in sorted((ROOT/'ports/nds').rglob('*')) if p.is_file() and p.suffix in ('.c','.h','.py')],
        artifacts=[artifact(target/f'dok_nds_sprites.{ext}') for ext in ('nds','elf','map')])
    (target/'build-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n','utf8')
    return target
def fixture(scans):
    old=(ROOT/'build/nds/n07/scene.dat').read_bytes();n=struct_unpack_count(old)
    records=[old[64+i*64:128+i*64] for i in range(n)]
    import struct
    for level,scan in enumerate(scans,1):
        x,z,h=scan['heavy']
        for frames,kind,dh in ((120,11,3),(154,12,0)):
            values=(len(records)+1,level,x,-300,z,h,0,0,dh,frames,2 if dh else 0,kind,x//480,z//480,0,0)
            records.append(struct.pack('<16I',*(v&0xffffffff for v in values)))
    records.append(struct.pack('<16I',len(records)+1,1,0,(-300)&0xffffffff,0,0,0,0,0,32,0,13,0,0,0,0))
    body=b''.join(records)+old[64+n*64:]
    data=b'N07SCN1\0'+struct.pack('<6I',1,len(records),64,10240,64+len(body),0)+hashlib.sha256(body).digest()+body
    (OUT/'scene.dat').write_bytes(data)
    return artifact(OUT/'scene.dat')
def struct_unpack_count(data):
    import struct
    return struct.unpack_from('<I',data,12)[0]
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--skip-build',action='store_true')
    parser.add_argument('--variant',choices=('ru','en','audit'),default='ru');parser.add_argument('--runtime',action='store_true')
    parser.add_argument('--core',type=Path);args=parser.parse_args()
    OUT.mkdir(parents=True,exist_ok=True);EVIDENCE.mkdir(parents=True,exist_ok=True)
    gate=dict(task='N12',passed=False,commands=[],sources=[],previous_pins=0)
    try:
        previous=json.loads((ROOT/'research/evidence/nds/perf/host-check.json').read_text('utf8'))
        superseded=json.loads((EVIDENCE/'baseline/source-supersession.json').read_text('utf8'))
        for row in previous['sources']+previous['arm9_build']['artifacts']:
            if digest(ROOT/row['path'])!=row['sha256']:
                archived=next(r for r in superseded if r['path']==row['path'])
                assert archived['sha256']==row['sha256']==digest(ROOT/archived['archive'])
            gate['previous_pins']+=1
        # The N07 renderer body is unchanged; N12 adds demand residency below it.
        backend=ROOT/'ports/nds/platform/render_probe/render_backend.c'
        assert backend.read_text('utf8').startswith((EVIDENCE/'baseline/render_backend.c').read_text('utf8'))
        gate['n07_backend_body_unchanged']=True
        from check_render_performance import ARCHIVED
        from prepare_interactions_fixture import INITIAL
        older=json.loads((ROOT/'research/evidence/nds/n11/host-check.json').read_text('utf8'))
        initial=json.loads(INITIAL.read_text('utf8'))
        proof=json.loads((ROOT/initial['source_supersession']['path']).read_text('utf8'))
        archives=[ROOT/'research/evidence/nds/perf'/p for p in ARCHIVED.values()]
        archives += [ROOT/r['archive'] for r in superseded]
        archives += [ROOT/r['archived']['path'] for r in proof['sources']]
        archive_hashes={digest(p) for p in archives if p.exists()}
        gate['historical_integrity']=dict(records=0,archived=0)
        for row in older['preserved']:
            path=Path(row['path']);path=path if path.is_absolute() else ROOT/path
            if digest(path)!=row['sha256']:
                assert row['sha256'] in archive_hashes,row['path']
                gate['historical_integrity']['archived']+=1
            gate['historical_integrity']['records']+=1
        env=dict(os.environ,ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-global-cache'),ZIG_LOCAL_CACHE_DIR=str(ROOT/'build/zig-local-cache'))
        zig=Path.home()/'AppData/Local/Programs/Zig/0.13.0/zig.exe'
        for name,extra in [('sprite_dump',[]),('test_sprites',[])]:
            exe=OUT/(name+'.exe')
            command=[zig,'cc',*FLAGS,*CORE,*SOURCES,f'ports/nds/tests/{name}.c','-o',exe]
            run(command,env=env);gate['commands'].append(list(map(str,command)))
        gate['unit']=json.loads(run([OUT/'test_sprites.exe']));assert not gate['unit']['failures']
        metric_command=[sys.executable,'-B','-X','utf8',ROOT/'ports/nds/tests/test_render_metrics.py']
        gate['metric_boundary_checks']=run(metric_command).strip();gate['commands'].append(list(map(str,metric_command)))
        gate['levels']=[];scans=[]
        for level in (1,2,3):
            inventory=json.loads(run([OUT/'sprite_dump.exe',ROOT/'build/nds/assets/dok-assets.ndspak',level]))
            assert inventory['frames']==154 and inventory['negative_intervals']==2
            from audit_sprites import resources
            import struct
            raw=resources('ru')
            for sequence in inventory['animations']:
                index=(sequence['id']>>18)&4095;data=raw[index];count=struct.unpack_from('<H',data)[0]
                assert len(sequence['frames'])==count;offset=8+count*10
                for ordinal,frame in enumerate(sequence['frames']):
                    interval,a,b=struct.unpack_from('<3h',data,8+ordinal*10)
                    width,height=struct.unpack_from('<2H',data,offset);size=struct.unpack_from('<I',data,offset+10)[0]
                    assert frame==[(1<<30)|(index<<18)|(4<<10)|ordinal,interval,a,b,width,height]
                    offset+=18+size
                assert offset==len(data)
            scan=json.loads(run([OUT/'sprite_dump.exe',ROOT/'build/nds/assets/dok-assets.ndspak',level,'--scan',1]));scans.append(scan)
            assert scan['passed'];gate['levels'].append(dict(level=level,inventory=inventory,scan=scan))
        gate['fixture']=fixture(scans)
        for script in ('audit_sprites.py','audit_sprite_world.py'):
            command=[sys.executable,'-B','-X','utf8',ROOT/'ports/nds/tools'/script,'--dump',OUT/'sprite_dump.exe']
            print(run(command),flush=True);gate['commands'].append(list(map(str,command)))
        target=OUT/args.variant
        if not args.skip_build:build(args.variant,2 if args.variant=='en' else 1,args.variant=='audit')
        gate['arm9_build']=json.loads((target/'build-manifest.json').read_text('utf8'))
        gate['sources']=[artifact(p) for p in sorted((ROOT/'ports/nds').rglob('*')) if p.is_file() and p.suffix in ('.c','.h','.py')]
        gate['passed']=True
    finally:(EVIDENCE/f'host-{args.variant}.json').write_text(json.dumps(gate,indent=2)+'\n','utf8')
    if args.runtime:
        command=[sys.executable,'-B','-X','utf8',ROOT/'ports/nds/tools/run_sprites_emulator.py','--target',target,'--packet',OUT/'scene.dat']
        if args.core:command+=['--core',args.core]
        log=target/'runtime.log'
        with log.open('w',encoding='utf8') as stream:result=subprocess.run(list(map(str,command)),stdout=stream,stderr=subprocess.STDOUT)
        print(f'Runtime exit {result.returncode}; {log}')
        return result.returncode
    print('N12 host/original/ARM9 gates passed');return 0
if __name__=='__main__':raise SystemExit(main())
