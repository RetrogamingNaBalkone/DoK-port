"""N19 HUD build provenance and explicit live presentation fixtures."""
import json,os,shutil,struct,hashlib,sys
from pathlib import Path
from check_items import ROOT,artifact,run
OUT=ROOT/'build/nds/n19';EVIDENCE=ROOT/'research/evidence/nds/n19'
def prepare():
    OUT.mkdir(parents=True,exist_ok=True);EVIDENCE.mkdir(parents=True,exist_ok=True)
    run([sys.executable,'-B','-X','utf8',ROOT/'ports/nds/tools/ui/extract_ui.py'])
    old=(ROOT/'build/nds/n18/scene.dat').read_bytes();n=struct.unpack_from('<I',old,12)[0]
    records=[bytearray(old[64+i*64:128+i*64]) for i in range(n)]
    catalog=json.loads((EVIDENCE/'ui-catalog.json').read_text('utf8'))
    stages={}
    for level in (1,2,3):
        row=bytearray(next(r for r in records if struct.unpack_from('<I',r,4)[0]==level and struct.unpack_from('<I',r,44)[0]==22))
        count=16+sum(len(m['plan']['pages']) for m in catalog['messages'] if m['source']=='native-door' or m.get('level')==level)
        stages[level]=count
        struct.pack_into('<I',row,0,59+level);struct.pack_into('<I',row,36,count*4);struct.pack_into('<I',row,44,28)
        records.append(row)
    body=b''.join(records)+old[64+n*64:]
    packet=b'N07SCN1\0'+struct.pack('<6I',1,len(records),64,10240,64+len(body),0)+hashlib.sha256(body).digest()+body
    (OUT/'scene.dat').write_bytes(packet)
    report=dict(task='N19',fixture=artifact(OUT/'scene.dat'),predecessor=artifact(ROOT/'analysis/nds-n18-audio.json'),stages=stages,scope='All accepted N18 cases plus explicit UI-only presentation fixtures over live worlds')
    (EVIDENCE/'inputs.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
def build(label):
    target=OUT/label;target.mkdir(parents=True,exist_ok=True);(target/'generated').mkdir(exist_ok=True)
    shutil.copyfile(ROOT/'build/nds/n07/generated/render_trig.inc',target/'generated/render_trig.inc')
    env=dict(os.environ,MSYSTEM='UCRT64',CHERE_INVOKING='1',MSYS2_PATH_TYPE='minimal')
    repo='/'+ROOT.as_posix()[0].lower()+ROOT.as_posix()[2:]
    command="set -eo pipefail\nsource /opt/wonderful/bin/wf-env\nwf-pacman -Q\ncd '"+repo+"'\nmake -C ports/nds -f Makefile.n19 OUTDIR=../../build/nds/n19/"+label+f' SPRITE_LANGUAGE={2 if label.endswith("en") else 1} SPRITE_AUDIT=0 GAMEPLAY_WATER_UV=1 GAMEPLAY_GEOMETRY_CACHE=0 GAMEPLAY_FRAME_STORE=1 GAMEPLAY_FAST_OVERLAY=1\n'
    log=run([Path.home()/'.local/msys64/usr/bin/bash.exe','--login','-s'],input=command,env=env)
    (target/'build.log').write_text(log,'utf8')
    lock=json.loads((ROOT/'ports/nds/toolchain_versions.json').read_text('utf8'));assert all(k+' '+v in log for k,v in lock['packages'].items())
    report=dict(task='N19',label=label,packages=lock['packages'],artifacts=[artifact(p) for p in target.iterdir() if p.suffix in ('.nds','.elf','.map')],sources=[artifact(p) for p in (ROOT/'ports/nds').rglob('*') if p.is_file() and (p.suffix in ('.c','.h','.inc','.py') or p.name.startswith('Makefile'))])
    report['generated_inputs']=[artifact(OUT/'generated/ui_native.inc'),artifact(OUT/'ui.dat'),artifact(OUT/'scene.dat')]
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
    report=dict(task='N19',passed=True,gameplay=test,source_callbacks=len(rows),original_sha256_before=before,original_sha256_after=originals(),linux_execution=False,scope='Accepted simulation compiled with UI/audio hooks disabled; UI source/host/ARM9 are separate checks',artifacts=[artifact(p) for p in target.iterdir()])
    (EVIDENCE/'gameplay-regression.json').write_text(json.dumps(report,indent=2)+'\n','utf8');print(json.dumps(test))
if __name__=='__main__':
    if sys.argv[1]=='prepare':prepare()
    elif sys.argv[1]=='host':host()
    else:build(sys.argv[1])
