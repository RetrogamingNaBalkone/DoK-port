"""Reproduce UI01 on the selected 2775 hardware renderer using shared tools."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',type=Path,default=Path('build/g20b'))
    parser.add_argument('--verify',action='store_true')
    args=parser.parse_args();os.chdir(ROOT)
    args.out.mkdir(parents=True,exist_ok=True)
    os.environ['ZIG_GLOBAL_CACHE_DIR']=str(ROOT/'build/zig-cache')
    os.environ['DOK_PROBE_FAST_VIDEO']='1'
    zig=Path(os.environ['LOCALAPPDATA'])/'Programs/Zig/0.13.0/zig.exe'
    commands=[]
    def run(name,argv):
        argv=list(map(str,argv));commands.append(dict(name=name,argv=argv))
        with (args.out/(name+'.log')).open('w',encoding='utf-8') as f:
            subprocess.run(argv,cwd=ROOT,stdout=f,stderr=subprocess.STDOUT,check=True)
        print(name,'passed',flush=True)
    common=[zig,'cc','-std=c11','-Wall','-Wextra','-Werror','-O2','-I','include']
    run('host-build',[*common,'src/ui.c','tests/test_ui.c','-o',args.out/'ui-tests.exe'])
    run('host',[str((args.out/'ui-tests.exe').resolve())])
    run('linux-build',[*common,'-target','x86_64-linux-musl','-static','src/ui.c','tests/test_ui.c','-o',args.out/'ui-tests-linux'])
    run('thumb-build',[*common,'-target','thumb-freestanding-eabi','-mcpu=arm7tdmi','-ffreestanding','-c','src/ui.c','-o',args.out/'ui-thumb.o'])
    run('base-build',['powershell','-NoProfile','-ExecutionPolicy','Bypass','-File','scripts/build-gba.ps1',
                      '-RendererProbe','-HalfColumns','-OutDir',args.out/'base'])
    run('hardware-build',[sys.executable,'scripts/prototype_gba_bsp.py','--base',args.out/'base',
                          '--out',args.out/'hardware','--variants','hw_v128_entity_rect'])
    build=args.out/'hardware/hw_v128_entity_rect'
    if args.verify:
        run('time-selftest-build',['powershell','-NoProfile','-ExecutionPolicy','Bypass','-File','scripts/build-gba.ps1',
                                  '-TimeProbe','-OutDir',args.out/'time-selftest'])
        run('time-selftest',[sys.executable,'scripts/verify_time_gba.py','--build-dir',args.out/'time-selftest',
                             '--report',args.out/'time-selftest.json'])
        for name,script,extra in (
            ('latency','verify_frontend_latency.py',[]),
            ('frontend','verify_frontend_gba.py',[]),('ui','verify_ui_gba.py',[]),('flow','verify_flow_gba.py',[]),
            ('audio','verify_audio_gba.py',[]),('time','verify_time_gba.py',[]),('arm','verify_performance_arm.py',[]),
            ('water','verify_gba_hardware_water.py',[]),('sprites','verify_sprites_gba.py',[]),
            ('transitions','verify_gba_hardware_transitions.py',['--mode4-transitions'])):
            run(name,[sys.executable,'scripts/'+script,'--build-dir',build,'--report',args.out/(name+'.json'),*extra])
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    sources=[p for folder in ('src','gba','include/dok') for p in (ROOT/folder).rglob('*') if p.suffix in ('.c','.h','.inc','.S','.ld')]
    sources += [ROOT/'scripts'/name for name in ('ui_copy.py','build_gba_frontend.py','prototype_gba_bsp.py','build-gba.ps1','verify_frontend_gba.py')]
    result=dict(task='G20B/UI01',rom_sha256=sha(build/'dok-diagnostic.gba'),rom_bytes=(build/'dok-diagnostic.gba').stat().st_size,
                build_dir=build.as_posix(),renderer='hw_v128_entity_rect / selected2775 architecture',verified=args.verify,
                host_executed=True,linux_compiled_only=True,thumb_compiled=True,commands=commands,
                sources={p.relative_to(ROOT).as_posix():sha(p) for p in sources},
                validation_reports={p.name:sha(p) for p in args.out.glob('*.json') if p.name!='frontend-build.json'})
    (args.out/'frontend-build.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(result['rom_sha256'],result['rom_bytes'])


if __name__=='__main__':main()
