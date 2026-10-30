"""Build the integrated N20 map runtime and pin accepted predecessor inputs."""
import hashlib,json,os,shutil,struct,sys
from pathlib import Path
from check_items import ROOT,artifact,run
OUT=ROOT/'build/nds/n20'; EVIDENCE=ROOT/'research/evidence/nds/n20'
def packet(records,blocks):
    body=b''.join(records)+blocks
    return b'N07SCN1\0'+struct.pack('<6I',1,len(records),64,10240,64+len(body),0)+hashlib.sha256(body).digest()+body
def supplemental_packets(predecessor):
    n=struct.unpack_from('<I',predecessor,12)[0];blocks=predecessor[64+n*64:]
    result={}
    for filename,source_case in (('scene-title.dat',42),('scene-combat-touch.dat',49)):
        row=bytearray(next(predecessor[64+i*64:128+i*64] for i in range(n) if struct.unpack_from('<I',predecessor,64+i*64)[0]==source_case))
        if source_case==49:struct.pack_into('<I',row,0,63);struct.pack_into('<I',row,36,1600)
        result[filename]=packet([row],blocks)
    return result
def prepare():
    OUT.mkdir(parents=True,exist_ok=True);EVIDENCE.mkdir(parents=True,exist_ok=True)
    old=(ROOT/'build/nds/n19/scene.dat').read_bytes();n=struct.unpack_from('<I',old,12)[0]
    assert n==62
    records=[bytearray(old[64+i*64:128+i*64]) for i in range(n)]
    row=bytearray(next(r for r in records if struct.unpack_from('<I',r,44)[0]==22))
    struct.pack_into('<I',row,0,63);struct.pack_into('<I',row,36,1600);struct.pack_into('<I',row,44,29)
    records.append(row);main_packet=packet(records,old[64+n*64:])
    assert len(main_packet)<65536
    (OUT/'scene.dat').write_bytes(main_packet)
    for name,data in supplemental_packets(old).items():(OUT/name).write_bytes(data)
    shutil.copyfile(ROOT/'build/nds/n19/ui.dat',OUT/'ui.dat')
    report=dict(task='N20',fixture=artifact(OUT/'scene.dat'),predecessor=artifact(ROOT/'analysis/nds-n19-hud.json'),
        added_case=dict(id=63,source_kind=29,presentations=1600),predecessor_cases_unchanged=True,
        lifetime=dict(catalog_bytes=114688,map_offset=65536,map_bytes=49152,packet_bytes=len(main_packet),
        session_borrows_catalog=False,session_fields='reader and header scalar/hash values; no workspace pointer'),
        files=[artifact(OUT/'ui.dat'),artifact(ROOT/'ports/nds/include/dok/assets.h'),artifact(ROOT/'ports/nds/core/assets.c')])
    (EVIDENCE/'inputs.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
def stage_supplemental():
    for label in ('title-smoke-ru','combat-touch-ru'):
        target=OUT/label;target.mkdir(parents=True,exist_ok=True)
        for name in ('dok_nds_gameplay.nds','dok_nds_gameplay.elf','dok_nds_gameplay.map'):
            shutil.copyfile(OUT/'final-release-ru'/name,target/name)
def build(label):
    target=OUT/label;target.mkdir(parents=True,exist_ok=True);(target/'generated').mkdir(exist_ok=True)
    shutil.copyfile(ROOT/'build/nds/n07/generated/render_trig.inc',target/'generated/render_trig.inc')
    env=dict(os.environ,MSYSTEM='UCRT64',CHERE_INVOKING='1',MSYS2_PATH_TYPE='minimal')
    repo='/'+ROOT.as_posix()[0].lower()+ROOT.as_posix()[2:]
    command="set -eo pipefail\nsource /opt/wonderful/bin/wf-env\nwf-pacman -Q\ncd '"+repo+"'\nmake -C ports/nds -f Makefile.n20 OUTDIR=../../build/nds/n20/"+label+f' SPRITE_LANGUAGE={2 if label.endswith("en") else 1} SPRITE_AUDIT=0 GAMEPLAY_WATER_UV=1 GAMEPLAY_GEOMETRY_CACHE=0 GAMEPLAY_FRAME_STORE=1 GAMEPLAY_FAST_OVERLAY=1\n'
    log=run([Path.home()/'.local/msys64/usr/bin/bash.exe','--login','-s'],input=command,env=env)
    (target/'build.log').write_text(log,'utf8')
    lock=json.loads((ROOT/'ports/nds/toolchain_versions.json').read_text('utf8'));assert all(k+' '+v in log for k,v in lock['packages'].items())
    report=dict(task='N20',label=label,packages=lock['packages'],artifacts=[artifact(p) for p in target.iterdir() if p.suffix in ('.nds','.elf','.map')],sources=[artifact(p) for p in (ROOT/'ports/nds').rglob('*') if p.is_file() and (p.suffix in ('.c','.h','.inc','.py') or p.name.startswith('Makefile'))],
        generated_inputs=[artifact(OUT/'scene.dat'),artifact(OUT/'ui.dat'),artifact(ROOT/'build/nds/n19/generated/ui_native.inc')])
    (target/'build-manifest.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    print(json.dumps(report['artifacts']))
def host():
    target=OUT/'host';target.mkdir(parents=True,exist_ok=True)
    env=dict(os.environ,ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-global-cache'),ZIG_LOCAL_CACHE_DIR=str(ROOT/'build/zig-local-cache'))
    zig=Path.home()/'AppData/Local/Programs/Zig/0.13.0/zig.exe'
    common=[zig,'cc','-std=c11','-pedantic-errors','-Wall','-Wextra','-Werror','-O2','-I','ports/nds/include','ports/nds/ui/map_control.c','ports/nds/tests/test_map_control.c']
    run([*common,'-o',target/'control.exe'],env=env);result=run([target/'control.exe'])
    run([*common,'-target','x86_64-linux-musl','-static','-o',target/'control-linux'],env=env)
    checks=int(result.split('checks=')[1].split()[0]);assert checks==174391
    report=dict(task='N20',passed=True,checks=checks,linux_execution=False,scope='All dimensions1..64, all five zooms, four bounds, gesture capture/re-touch, drag, pause cancellation, center, clamp saturation, follow/nonfollow zoom anchor and out-of-screen capture cancellation through release',
        artifacts=[artifact(target/'control.exe'),artifact(target/'control-linux')],sources=[artifact(ROOT/p) for p in ('ports/nds/ui/map_control.c','ports/nds/include/dok/map_control.h','ports/nds/tests/test_map_control.c')])
    (EVIDENCE/'host-control.json').write_text(json.dumps(report,indent=2)+'\n','utf8');print(json.dumps(report))
if __name__=='__main__':
    if sys.argv[1]=='prepare':prepare()
    elif sys.argv[1]=='host':host()
    elif sys.argv[1]=='stage-supplemental':stage_supplemental()
    else:build(sys.argv[1])
