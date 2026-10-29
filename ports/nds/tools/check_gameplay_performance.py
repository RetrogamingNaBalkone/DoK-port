"""N17P build/fixture provenance. Previous task reports are immutable inputs."""
import json,os,shutil,struct,hashlib,sys
from pathlib import Path
from check_items import ROOT,artifact,run
OUT=ROOT/'build/nds/n17p';EVIDENCE=ROOT/'research/evidence/nds/n17p'
def prepare():
    OUT.mkdir(parents=True,exist_ok=True);EVIDENCE.mkdir(parents=True,exist_ok=True)
    pins=[]
    for name in ('nds-n13p-performance.json','nds-n15-combat.json','nds-n16-ai-movement.json','nds-n17-ai-combat.json'):
        p=ROOT/'analysis'/name
        if not p.exists():
            if 'n13p' in name:p=ROOT/'analysis/nds-n13p-performance.json'
        accepted=json.loads(p.read_text('utf8'));assert accepted['passed'];pins.append(artifact(p))
        for row in accepted['artifacts']:assert artifact(ROOT/row['path'])['sha256']==row['sha256'],row['path']
    old=(ROOT/'build/nds/n13p/scene.dat').read_bytes();n=struct.unpack_from('<I',old,12)[0]
    records=[old[64+i*64:128+i*64] for i in range(n)]
    # Actual source starts, collision-resolved motion/input routes. Arena/all-awake
    # are separately labelled and never included in the normal cadence gate.
    levels=json.loads((ROOT/'research/evidence/nds/n13/host-check.json').read_text())['levels']
    starts=[]
    for level in range(1,4):
        block=old[64+n*64+(level-1)*10240:64+n*64+level*10240]
        # Source start is supplied by render scan, same as accumulated case1/2/3.
        source=next(struct.unpack('<16I',r) for r in records if struct.unpack('<16I',r)[1]==level)
        x,y,z,h=source[2:6];starts.append((level,x,y,z,h))
        for kind,frames in ((22,240),(25,120)):
            records.append(struct.pack('<16I',len(records)+1,level,x,y,z,h,0,0,0,frames,0,kind,0,0,0,0))
    from audit_geometry import NativeCellOracle
    for level in range(1,4):
        native=NativeCellOracle(level);objects={o['glyph']:o for o in native.base['objects']}
        maps=[bytes.fromhex(p) for p in native.world['maps']];width=native.base['settings'][0]
        weapon=next(i for i,g in enumerate(maps[1]) if objects[g]['class_id']==10)
        def add(kind,cell,h,frames=240):
            x=(cell%width)*480+240;z=(cell//width)*480+240
            records.append(struct.pack('<16I',len(records)+1,level,x,(-300)&0xffffffff,z,h,0,0,0,frames,0,kind,0,0,0,0))
        add(23,weapon,0)
        enemies=[o for o in native.world['objects'] if o['class_id']==9 and o['pose_known']]
        def adjacent(cell):
            x,z=cell%width,cell//width
            for dx,dz,h in ((0,1,180),(1,0,90),(0,-1,0),(-1,0,270)):
                q=(z+dz)*width+x+dx
                if 0<x+dx<63 and 0<z+dz<63 and objects[maps[1][q]]['class_id']==0 and objects[maps[0][q]]['class_id']==0:return q,h
            return None
        enemy=next((e for e in enemies if adjacent(e['z']//480*width+e['x']//480)),None);assert enemy
        cell,h=adjacent(enemy['z']//480*width+enemy['x']//480)
        add(24,cell,h,360);add(26,cell,h,240)
        door=next(i for i,g in enumerate(maps[1]) if objects[g]['class_id']==7 and adjacent(i))
        cell,h=adjacent(door);add(27,cell,h,240)
    body=b''.join(records)+old[64+n*64:]
    packet=b'N07SCN1\0'+struct.pack('<6I',1,len(records),64,10240,64+len(body),0)+hashlib.sha256(body).digest()+body
    (OUT/'scene.dat').write_bytes(packet)
    report=dict(task='N17P',pins=pins,fixture=artifact(OUT/'scene.dat'),inherited=n,cases=len(records),starts=starts)
    (EVIDENCE/'inputs.json').write_text(json.dumps(report,indent=2)+'\n','utf8');return report
def build(label):
    target=OUT/label;target.mkdir(parents=True,exist_ok=True);(target/'generated').mkdir(exist_ok=True)
    shutil.copyfile(ROOT/'build/nds/n07/generated/render_trig.inc',target/'generated/render_trig.inc')
    env=dict(os.environ,MSYSTEM='UCRT64',CHERE_INVOKING='1',MSYS2_PATH_TYPE='minimal')
    repo='/'+ROOT.as_posix()[0].lower()+ROOT.as_posix()[2:]
    command="set -eo pipefail\nsource /opt/wonderful/bin/wf-env\nwf-pacman -Q\ncd '"+repo+"'\nmake -C ports/nds -f Makefile.n17p OUTDIR=../../build/nds/n17p/"+label+f' SPRITE_LANGUAGE={2 if label.endswith("en") else 1} SPRITE_AUDIT={int("audit" in label)} GAMEPLAY_WATER_UV={int(label.startswith(("water","final")))} GAMEPLAY_GEOMETRY_CACHE={int(label.startswith("cache"))} GAMEPLAY_FRAME_STORE={int(label.startswith("final"))} GAMEPLAY_FAST_OVERLAY={int(label.startswith("final"))}\n'
    log=run([Path.home()/'.local/msys64/usr/bin/bash.exe','--login','-s'],input=command,env=env)
    (target/'build.log').write_text(log,'utf8')
    lock=json.loads((ROOT/'ports/nds/toolchain_versions.json').read_text('utf8'));assert all(k+' '+v in log for k,v in lock['packages'].items())
    report=dict(task='N17P',label=label,packages=lock['packages'],artifacts=[artifact(p) for p in target.iterdir() if p.suffix in ('.nds','.elf','.map')],sources=[artifact(p) for p in (ROOT/'ports/nds').rglob('*') if p.is_file() and (p.suffix in ('.c','.h','.inc','.py') or p.name.startswith('Makefile'))])
    (target/'build-manifest.json').write_text(json.dumps(report,indent=2)+'\n','utf8');print(json.dumps(report['artifacts']))
    for row in report['sources']:
        source=ROOT/row['path'];destination=EVIDENCE/'sources'/label/row['path'];destination.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,destination)
def host():
    from check_combat import SOURCES
    from check_interactions import FLAGS
    env=dict(os.environ,ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-global-cache'),ZIG_LOCAL_CACHE_DIR=str(ROOT/'build/zig-local-cache'))
    zig=Path.home()/'AppData/Local/Programs/Zig/0.13.0/zig.exe'
    modules=[*SOURCES,'ports/nds/core/ai.c','ports/nds/render/effects.c','ports/nds/render/effect_cache.c','ports/nds/render/water_texture.c','ports/nds/render/frame_store.c','ports/nds/render/texture_cache.c','ports/nds/render/weapon_overlay_fast.c']
    report=dict(task='N17P',passed=False,tests={})
    for name in ('test_frame_store','test_geometry_cache','test_gameplay_runtime','test_weapon_overlay_fast'):
        source=modules[:];extra=[]
        if name=='test_geometry_cache':
            run([zig,'cc',*FLAGS,'-Ddok_geometry_build_cell=dok_geometry_build_cell_uncached','-c','ports/nds/render/geometry.c','-o',OUT/'geometry-reference.o'],env=env)
            source=[s for s in source if not s.endswith('/geometry.c')];extra=[OUT/'geometry-reference.o','ports/nds/render/geometry_cache.c']
        if name=='test_gameplay_runtime':extra=['ports/nds/platform/gameplay_probe/gameplay.c','ports/nds/platform/gameplay_probe/effects_runtime.c']
        command=[zig,'cc',*FLAGS,'-I','ports/nds/platform/gameplay_probe',*source,*extra,f'ports/nds/tests/{name}.c','-o',OUT/(name+'.exe')]
        run(command,env=env)
        args=[OUT/(name+'.exe')]+([ROOT/'build/nds/assets/dok-assets.ndspak'] if name!='test_weapon_overlay_fast' else [])
        if name=='test_gameplay_runtime':args+=[EVIDENCE/'callbacks.jsonl']
        report['tests'][name]=json.loads(run(args))
    report['native']=json.loads(run([sys.executable,'-B','-X','utf8',ROOT/'ports/nds/tools/audit_gameplay.py']))
    command=[zig,'cc',*FLAGS,'-I','ports/nds/platform/gameplay_probe','-target','x86_64-linux-musl','-static',*modules,'ports/nds/platform/gameplay_probe/gameplay.c','ports/nds/platform/gameplay_probe/effects_runtime.c','ports/nds/tests/test_gameplay_runtime.c','-o',OUT/'test_gameplay_runtime-linux']
    run(command,env=env);report['linux_execution']=False;report['passed']=True
    report['sources']=[artifact(p) for p in (ROOT/'ports/nds').rglob('*') if p.is_file() and (p.suffix in ('.c','.h','.inc','.py') or p.name.startswith('Makefile'))]
    (EVIDENCE/'host-check.json').write_text(json.dumps(report,indent=2)+'\n','utf8');print(json.dumps(report['tests']))
if __name__=='__main__':
    if sys.argv[1]=='prepare':print(json.dumps(prepare()))
    elif sys.argv[1]=='host':host()
    else:build(sys.argv[1])
