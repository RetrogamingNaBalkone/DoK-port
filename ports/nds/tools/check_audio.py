"""N18 audio build provenance and explicit live stress fixture."""
import json,os,shutil,struct,hashlib,sys
from pathlib import Path
from check_items import ROOT,artifact,run
OUT=ROOT/'build/nds/n18';EVIDENCE=ROOT/'research/evidence/nds/n18'
def prepare():
    OUT.mkdir(parents=True,exist_ok=True);EVIDENCE.mkdir(parents=True,exist_ok=True)
    old=(ROOT/'build/nds/n17p/scene.dat').read_bytes();n=struct.unpack_from('<I',old,12)[0]
    records=[bytearray(old[64+i*64:128+i*64]) for i in range(n)]
    # Keep every accepted route. Extend one live movement route so the entire
    # 43-second title stream, including all eight source chunks, runs under AI.
    row=next(r for r in records if struct.unpack_from('<I',r,44)[0]==22)
    struct.pack_into('<I',row,36,1800)
    body=b''.join(records)+old[64+n*64:]
    packet=b'N07SCN1\0'+struct.pack('<6I',1,n,64,10240,64+len(body),0)+hashlib.sha256(body).digest()+body
    (OUT/'scene.dat').write_bytes(packet)
    report=dict(task='N18',fixture=artifact(OUT/'scene.dat'),predecessor=artifact(ROOT/'analysis/nds-n17p-performance.json'),scope='All N17P cases; one explicit title-audio under live gameplay stress')
    (EVIDENCE/'inputs.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
def build(label):
    target=OUT/label;target.mkdir(parents=True,exist_ok=True);(target/'generated').mkdir(exist_ok=True)
    shutil.copyfile(ROOT/'build/nds/n07/generated/render_trig.inc',target/'generated/render_trig.inc')
    env=dict(os.environ,MSYSTEM='UCRT64',CHERE_INVOKING='1',MSYS2_PATH_TYPE='minimal')
    repo='/'+ROOT.as_posix()[0].lower()+ROOT.as_posix()[2:]
    command="set -eo pipefail\nsource /opt/wonderful/bin/wf-env\nwf-pacman -Q\ncd '"+repo+"'\nmake -C ports/nds -f Makefile.n18 OUTDIR=../../build/nds/n18/"+label+f' SPRITE_LANGUAGE={2 if label.endswith("en") else 1} SPRITE_AUDIT=0 GAMEPLAY_WATER_UV=1 GAMEPLAY_GEOMETRY_CACHE=0 GAMEPLAY_FRAME_STORE=1 GAMEPLAY_FAST_OVERLAY=1\n'
    log=run([Path.home()/'.local/msys64/usr/bin/bash.exe','--login','-s'],input=command,env=env)
    (target/'build.log').write_text(log,'utf8')
    lock=json.loads((ROOT/'ports/nds/toolchain_versions.json').read_text('utf8'));assert all(k+' '+v in log for k,v in lock['packages'].items())
    report=dict(task='N18',label=label,packages=lock['packages'],artifacts=[artifact(p) for p in target.iterdir() if p.suffix in ('.nds','.elf','.map')],sources=[artifact(p) for p in (ROOT/'ports/nds').rglob('*') if p.is_file() and (p.suffix in ('.c','.h','.inc','.py') or p.name.startswith('Makefile'))])
    (target/'build-manifest.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    for row in report['sources']:
        source=ROOT/row['path'];dest=EVIDENCE/'sources'/label/row['path'];dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,dest)
    print(json.dumps(report['artifacts']))
def host():
    from check_combat import SOURCES
    from check_interactions import FLAGS
    from audit_ai_combat import Original,models,trace_fields,originals
    target=OUT/'regression';target.mkdir(parents=True,exist_ok=True)
    env=dict(os.environ,ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-global-cache'),ZIG_LOCAL_CACHE_DIR=str(ROOT/'build/zig-local-cache'))
    zig=Path.home()/'AppData/Local/Programs/Zig/0.13.0/zig.exe'
    modules=[*SOURCES,'ports/nds/core/ai.c','ports/nds/render/effects.c','ports/nds/render/effect_cache.c','ports/nds/render/water_texture.c','ports/nds/render/frame_store.c','ports/nds/render/texture_cache.c','ports/nds/render/weapon_overlay_fast.c']
    extra=['ports/nds/platform/gameplay_probe/gameplay.c','ports/nds/platform/gameplay_probe/effects_runtime.c','ports/nds/tests/test_gameplay_runtime.c']
    command=[zig,'cc',*FLAGS,'-I','ports/nds/platform/gameplay_probe',*modules,*extra,'-o',target/'test_gameplay_runtime.exe']
    run(command,env=env);test=json.loads(run([target/'test_gameplay_runtime.exe',ROOT/'build/nds/assets/dok-assets.ndspak',target/'callbacks.jsonl']))
    before=originals();model=models();fields=trace_fields();rows=[dict(zip(fields,json.loads(s),strict=True)) for s in (target/'callbacks.jsonl').read_text().splitlines()]
    for language in (1,2):
        native=Original(language)
        for row in rows:
            if row['language']==language:native.callback(row,model[language,row['level'],row['glyph']])
    assert before==originals()
    run([zig,'cc',*FLAGS,'-I','ports/nds/platform/gameplay_probe','-target','x86_64-linux-musl','-static',*modules,*extra,'-o',target/'test_gameplay_runtime-linux'],env=env)
    report=dict(task='N18',passed=True,gameplay=test,source_callbacks=len(rows),original_sha256_before=before,original_sha256_after=originals(),linux_execution=False,scope='Accepted simulation compiled with audio hooks disabled; audio transport/source are separate checks',artifacts=[artifact(p) for p in target.iterdir()])
    (EVIDENCE/'gameplay-regression.json').write_text(json.dumps(report,indent=2)+'\n','utf8');print(json.dumps(test))
if __name__=='__main__':
    if sys.argv[1]=='prepare':prepare()
    elif sys.argv[1]=='host':host()
    else:build(sys.argv[1])
