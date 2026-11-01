"""Reproduce G21 regression and package a private, unapproved release candidate.

Run from the repository root with the shared Python/Zig/mGBA installations.
Original EXE/RES and verified G02 runtime exports are required locally.
No publishing, source-data distribution or release acceptance is performed.
"""
import argparse
import ctypes as ct
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'ports/gba/scripts'))
from verify_gba import sha,SystemInfo

BASE_REPORTS=('latency','frontend','ui','flow','credits','audio','time','arm','water','sprites','transitions')
EXTRA=(('combat','verify_combat_gba.py',()),
       ('ai-combat','verify_ai_combat_gba.py',()),
       ('projectile-cells','verify_projectile_cells_gba.py',('reference','--reference-fixed')),
       ('close-hits','verify_close_hits_gba.py',('reference','--ordinary-pickup')),
       ('ceiling-sides','verify_ceiling_sides_gba.py',()),
       ('story','verify_story_gba.py',()),
       ('automap','verify_automap_gba.py',()))


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out',type=Path,default=Path('build/gba/g21-final'))
    ap.add_argument('--repeat',type=Path,default=Path('build/gba/g21-repeat'))
    ap.add_argument('--assets',type=Path,default=Path('build/gba/g21-clean-assets'))
    ap.add_argument('--ceiling',type=Path,default=Path('build/gba/g21-ceiling-sweep'))
    ap.add_argument('--stage',choices=('all','build','checks','assemble'),default='all',
                    help='assemble validates recorded pins; it does not run or replace tests')
    args=ap.parse_args()
    os.chdir(ROOT)
    args.out.mkdir(parents=True,exist_ok=True)
    build=args.out/'hardware/hw_v128_vblank_only'
    repeat=args.repeat/'hardware/hw_v128_vblank_only'
    py=[sys.executable,'-X','utf8']
    commands=[]

    def run(name,argv):
        argv=list(map(str,argv))
        commands.append(dict(name=name,argv=argv))
        with (args.out/(name+'.log')).open('w',encoding='utf-8') as log:
            subprocess.run(argv,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
        print(name,'passed',flush=True)

    def script(name):
        return [*py,'ports/gba/scripts/'+name]

    originals=[Path('originals/pkr')/folder/name for folder,prefix in (('RUS','PKR'),('ENG','PKE')) for name in (prefix+'.EXE',prefix+'.RES')]
    before={p.as_posix():sha(p) for p in originals}
    for language in ('ru','en'):
        inventory=read('analysis/originals/inventory-'+language+'.json')
        assert sha(inventory['file'])==inventory['sha256']
        assert sha(inventory['resource']['file'])==inventory['resource']['sha256']
    if args.stage in ('all','build'):
        run('pack',[*script('pack_gba_assets.py'),'--output',args.assets])
        run('host-all',['powershell','-NoProfile','-ExecutionPolicy','Bypass','-File',
                        'ports/gba/scripts/build-native.ps1','-CrossChecks'])
        run('packing',[*script('verify_packing.py'),'--report',args.out/'packing.json'])
        run('original-combat',script('verify_combat_original.py'))
        run('original-surfaces',script('verify_surfaces.py'))
        run('frontend-all',[*script('build_gba_frontend.py'),'--out',args.out,'--assets-bin',args.assets/'assets.bin','--verify'])
        run('repeat',[*script('build_gba_frontend.py'),'--out',args.repeat,'--assets-bin',args.assets/'assets.bin'])
    if args.stage in ('all','checks'):
        for name,source,extra in EXTRA:
            argv=[*script(source),'--build-dir',build,'--report',args.out/(name+'.json')]
            for value in extra:
                argv.extend(['--reference-build',str(repeat)] if value=='reference' else [value])
            run(name,argv)
        run('bat-hits',[*script('audit_g21_bat_hits.py'),'--build-dir',build,'--report',args.out/'bat-hits.json'])
        run('ceiling',[*script('audit_g21_ceiling.py'),'--build-dir',build,'--out',args.ceiling])
    command_file=args.out/('commands-'+args.stage+'.json')
    command_file.write_text(json.dumps(commands,indent=2)+'\n',encoding='utf-8')
    assert before=={p.as_posix():sha(p) for p in originals},'Original samples changed'
    if args.stage in ('build','checks'):
        return

    rom=build/'dok-diagnostic.gba'
    rom_sha=sha(rom)
    assert rom_sha==sha(repeat/'dok-diagnostic.gba')
    current=read(args.out/'frontend-build.json')
    assert current['verified'] and current['rom_sha256']==rom_sha
    for path,value in current['sources'].items():
        assert sha(path)==value,('stale build input',path)
    assert current['assets_sha256']==sha(args.assets/'assets.bin')
    assert sha(args.assets/'assets.bin')==sha('build/gba/assets/assets.bin')
    reports={}
    for name in (*BASE_REPORTS,*(row[0] for row in EXTRA),'bat-hits'):
        path=args.out/(name+'.json')
        report=read(path)
        assert report.get('status','passed')=='passed',(name,report.get('status'))
        if 'rom_sha256' in report:
            assert report['rom_sha256']==rom_sha,name
        if 'after' in report:
            assert report['after']['rom_sha256']==rom_sha,name
        script_path='ports/gba/scripts/'+next((r[1] for r in EXTRA if r[0]==name),
            'audit_g21_bat_hits.py' if name=='bat-hits' else
            'verify_frontend_latency.py' if name=='latency' else
            'verify_performance_arm.py' if name=='arm' else
            'verify_gba_hardware_water.py' if name=='water' else
            'verify_gba_hardware_transitions.py' if name=='transitions' else 'verify_'+name+'_gba.py')
        if 'script_sha256' in report:
            assert report['script_sha256']==sha(script_path),('stale test',name)
        reports[name]=dict(path=path.as_posix(),sha256=sha(path))
    ceiling=read(args.ceiling/'ceiling.json')
    assert ceiling['status']=='passed' and ceiling['rom_sha256']==rom_sha
    assert ceiling['script_sha256']==sha('ports/gba/scripts/audit_g21_ceiling.py')
    reports['ceiling']=dict(path=(args.ceiling/'ceiling.json').as_posix(),sha256=sha(args.ceiling/'ceiling.json'))
    packing=read(args.out/'packing.json')
    assert packing['status']=='passed'
    assert packing['checker_sha256']==sha('ports/gba/scripts/verify_packing.py')
    assert packing['packer_sha256']==sha('ports/gba/scripts/pack_gba_assets.py')
    assert packing['determinism']['assets.bin']['sha256']==current['assets_sha256']
    reports['packing']=dict(path=(args.out/'packing.json').as_posix(),sha256=sha(args.out/'packing.json'))
    for name,path in (('time-selftest',args.out/'time-selftest.json'),
                      ('original-combat',Path('analysis/gba/g13-original-verification.json')),
                      ('original-surfaces',Path('analysis/gba/g10-original-verification.json'))):
        value=read(path)
        if name=='time-selftest':
            assert value['clock_runtime_passed']
            assert value['rom_sha256']==sha(args.out/'time-selftest/dok-diagnostic.gba')
        else:
            assert value['status']=='passed'
        reports[name]=dict(path=path.as_posix(),sha256=sha(path))
    bats=read(args.out/'bat-hits.json')
    issue_status={
        'BUG01':'Grid-boundary misses reproduced with centered blaster aim; original cell lookup and motion confirmed. No gameplay correction applied.',
        'BUG02':'Native affine LIGHT seams reproduced in geometry; the reported full dark column has not been uniquely identified or corrected.'}
    core_path=Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll'
    core=ct.CDLL(str(core_path))
    info=SystemInfo()
    core.retro_get_system_info(ct.byref(info))
    report=dict(task='G21',status='regression_passed_acceptance_open',
        rom_sha256=rom_sha,rom_bytes=rom.stat().st_size,
        repeated_rom_exact=True,package_sha256=sha(args.assets/'assets.bin'),
        originals_sha256_before=before,originals_sha256_after={p.as_posix():sha(p) for p in originals},
        tools=dict(python=sys.version,zig=subprocess.check_output([str(Path(os.environ['LOCALAPPDATA'])/'Programs/Zig/0.13.0/zig.exe'),'version'],text=True).strip(),
                   unicorn=__import__('unicorn').__version__,
                   emulator=info.name.decode()+' '+info.version.decode(),core_sha256=sha(core_path)),
        reports=reports,build_manifest_sha256=sha(args.out/'frontend-build.json'),
        host_log_sha256=sha(args.out/'host-all.log'),
        bat_cases=len(bats['rows']),bat_results=bats['counts'],
        native_motion_steps=sum(len(p['endpoints']) for p in bats['native_movement_paths']),
        ceiling_frames=ceiling['frames'],ceiling_potential_seam_frames=ceiling['frames_with_potential_seams'],
        native_ceiling_potential_seam_frames=ceiling['original_frames_with_potential_seams'],
        known_issues=issue_status,
        physical_gba_tested=False,complete_navigation_playthrough=False,
        current_rom_human_listening=False,release_approved=False,published=False,
        next_id='G21',gba_budget_G20_closed=False,
        sources={p.as_posix():sha(p) for folder in ('ports/gba/src','ports/gba/platform','ports/gba/include','ports/gba/scripts') for p in Path(folder).rglob('*') if p.is_file() and p.suffix in ('.c','.h','.inc','.S','.ld','.py','.ps1')})
    destination=Path('analysis/gba/g21-regression-20261101.json')
    destination.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    artifacts=Path('artifacts/gba')
    artifacts.mkdir(parents=True,exist_ok=True)
    candidate=artifacts/'DoK-port_G21_Candidate.gba'
    shutil.copyfile(rom,candidate)
    manifest={k:v for k,v in report.items() if k not in ('sources','reports','host_log_sha256','build_manifest_sha256')}
    manifest['verification_report_sha256']=sha(destination)
    text=json.dumps(manifest,indent=2)+'\n'
    (artifacts/'DoK-port_G21_Candidate.json').write_text(text,encoding='utf-8')
    readme=('DoK-port G21 candidate / 2026-11-01\n\n'
        'One ROM: Russian/English selection, three independent levels.\n'
        'D-pad: move/turn; L/R: strafe; B: run; A: fire; Start: use; Select: map.\n'
        'L+R: weapon selection; Start+Select: pause.\n'
        'Menus: A/Start confirm, B back. Text screens: A/Start next, B back, Select language.\n\n'
        'Private candidate; G21 acceptance remains open. No new public release.\n'
        'Automated mGBA regression passed; this ROM has no new physical GBA verification.\n'
        'Known: grid-boundary blaster misses; reported ceiling dark column remains unresolved.\n'
        'Speed varies by scene; stable 30 FPS is not promised.\n'
        'No original EXE/RES, extracted resource package or decompiled sources are included.\n')
    archive=artifacts/'DoK-port_G21_Candidate.zip'
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
        for name,data in ((candidate.name,candidate.read_bytes()),('manifest.json',text.encode()),('README.txt',readme.encode())):
            info=zipfile.ZipInfo(name,date_time=(2026,11,1,0,0,0))
            info.compress_type=zipfile.ZIP_DEFLATED
            z.writestr(info,data)
    with zipfile.ZipFile(archive) as z:
        assert z.testzip() is None and len(z.namelist())==3
        assert hashlib.sha256(z.read(candidate.name)).hexdigest()==rom_sha
    print(report['status'],rom_sha,rom.stat().st_size,flush=True)


if __name__=='__main__':
    main()
