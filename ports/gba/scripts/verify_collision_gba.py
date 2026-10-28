"""G08 exact fixed-step replay, real GBA buttons, language and reset checks."""

import sys as _layout_sys
from pathlib import Path as _LayoutPath
_layout_root = _LayoutPath(__file__).resolve().parents[3]
for _layout_dir in ("scripts", "tools/originals", "tools/reports", "ports/gba/scripts"):
    _layout_sys.path.append(str(_layout_root / _layout_dir))

import argparse
import ctypes as ct
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
from gba_rom import elf_info
from verify_gba import Frontend,sha
from verify_renderer_gba import NAMES as RENDER_NAMES,stack_usage
from verify_movement_gba import NAMES

def fnv(data):
    h=2166136261
    for b in data:h=((h^b)*16777619)&0xffffffff
    return h

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build-dir',type=Path,default=Path('build/gba/g08'))
    p.add_argument('--core',type=Path,default=Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll')
    a=p.parse_args();build=a.build_dir
    _,symbols,_=elf_info(build/'dok-diagnostic.elf');meta=json.loads((build/'build-report.json').read_text())
    fe=Frontend(a.core.resolve(),build);fe.load(build/'dok-diagnostic.gba');samples=[];captures=[]
    def motion():
        values=list(struct.unpack('<25I',fe.read(symbols['g_motion_diag'],100)))
        for i in list(range(8,17))+[22]:
            if values[i]>=2**31:values[i]-=2**32
        return dict(zip(NAMES,values))
    def render():return dict(zip(RENDER_NAMES,struct.unpack('<32I',fe.read(symbols['g_render_diag'],128))))
    def write(address,data):ct.memmove(fe.pointer(address,len(data)),data,len(data))
    def until(predicate,buttons=(),limit=1500):
        for _ in range(limit):
            fe.run(1,buttons);d=motion();r=render();basic=fe.diag(symbols['g_diag'])
            if basic['magic']==0x444f4b31:assert not basic['fault'],(basic,d,r)
            if d['magic']==0x4d4f5437:assert not d['error'],d
            if r['draws'] and r['magic']==0x524e4435:
                assert r['status']==0 and r['guard_ok']==1,r
                if not samples or samples[-1]['draws']!=r['draws']:
                    samples.append(dict(draws=r['draws'],cycles=r['last_cycles'],motion_cycles=d['last_cycles'],
                        test_mode=struct.unpack('<I',fe.read(symbols['g_motion_probe'],4))[0]))
            if d['magic']==0x4d4f5437 and predicate(d):return d
        raise AssertionError(('timeout',d,r))
    def ready():
        d=until(lambda d:render()['draws']>=3)
        assert (d['x'],d['y'],d['z'],d['heading'])==(15120,-300,22800,90),d
        return d
    def capture(name):
        target=render()['draws']+2;until(lambda d:render()['draws']>=target and render()['phase']==2)
        fe.screenshot(build/f'collision-{name}.png')
        captures.append(dict(name=name,pose=motion(),rgb_sha256=hashlib.sha256(fe.rgb).hexdigest()))
    def maps():
        # ARM32 dok_runtime: maps at 17472, 4096 cells per plane (G03 LEVEL1).
        return fe.read(symbols['g_render_world']+17472,12288)
    def snapshot(d):return [d[k] for k in ('x','y','z','heading','direction','step','accumulator_a','accumulator_b','command','step_distance','motion_flags','remembered_flags')]
    ready();capture('start');traces=[];final_maps=[]
    sequence=[(16,64),(0,8),(32,64),(0x80000,64),(0x100000,64),(0x2010,128),
              (4,64),(16,80),(0x80010,80),(8,64),(0x2020,80),(0,8),
              (20,100),(0x100010,80),(0,8),(48,8),(0x180000,8)]
    for language in ('ru','en'):
        fe.core.retro_reset();ready();write(symbols['g_motion_probe'],struct.pack('<I',1))
        until(lambda d:d['held']==0)
        if language=='en':
            before=fe.read(symbols['g_render_world']+4,33860);switches=render()['switches']
            until(lambda d:render()['switches']>switches,[2]);until(lambda d:d['held']==0)
            assert fe.read(symbols['g_render_world']+4,33860)==before,'Language mutated world'
        expected=subprocess.check_output(['build/gba/host/dok-collision-replay.exe','build/gba/assets/assets.bin',language],
             input=''.join(f'{cmd} {count}\n' for cmd,count in sequence),text=True)
        expected=[list(map(int,line.split())) for line in expected.splitlines()];trace=[]
        for request,((commands,count),want) in enumerate(zip(sequence,expected),1):
            write(symbols['g_motion_probe']+12,struct.pack('<II',commands,count))
            write(symbols['g_motion_probe']+4,struct.pack('<I',request))
            d=until(lambda d:struct.unpack('<I',fe.read(symbols['g_motion_probe']+8,4))[0]==request)
            got=snapshot(d)+[fnv(maps())];assert got==want,(language,commands,want,got)
            assert 238<=d['x']<=30481 and 238<=d['z']<=30481
            trace.append(dict(commands=commands,count=count,actual=got))
        capture('replay-'+language);traces.append(trace);final_maps.append(maps())
    assert traces[0]==traces[1] and final_maps[0]==final_maps[1]
    fe.core.retro_reset();ready();live=[]
    # Actual Up goes west from heading90; hold against the first corridor wall.
    for name,buttons,held,command in [('forward-wall',[4],64,16),('back-wall',[5],128,32),
            ('run-wall',[0,4],66,0x2010),('strafe',[10],512,0x80000),('turn',[6],32,4)]:
        before=motion();d=until(lambda d:d['held']==held and d['steps']>=before['steps']+80,buttons)
        assert d['commands']==command
        if name.endswith('wall'):
            wall_pose=(d['x'],d['z']);stopped=until(lambda x:x['steps']>=d['steps']+20,buttons)
            assert (stopped['x'],stopped['z'])==wall_pose,(name,d,stopped)
            live.append(dict(name=name,wall=d,held_again=stopped))
        else:live.append(dict(name=name,actual=d))
        released=until(lambda d:d['held']==0 and d['step']==0)
        end=until(lambda d:d['steps']>=released['steps']+8)
        assert (end['x'],end['y'],end['z'])==(released['x'],released['y'],released['z'])
        capture(name)
    assert captures[0]['rgb_sha256']!=captures[-1]['rgb_sha256']
    stopped=motion();before=fe.read(symbols['g_render_world']+4,33860);switches=render()['switches']
    until(lambda d:render()['switches']>switches,[2]);until(lambda d:d['held']==0)
    assert snapshot(motion())==snapshot(stopped) and fe.read(symbols['g_render_world']+4,33860)==before
    stack=stack_usage(fe);fe.core.retro_reset()
    ct.memset(fe.pointer(0x02000000,meta['ewram_static_bytes']),0xa5,meta['ewram_static_bytes']);restart=ready()
    assert fe.read(symbols['g_motion_probe'],20)==bytes(20)
    report=dict(task='G08',status='passed',rom_sha256=sha(build/'dok-diagnostic.gba'),
        replay=traces[0],replay_total_steps=sum(n for _,n in sequence),languages=['ru','en'],
        world_unchanged_on_language=True,live_buttons=live,captures=captures,stack=stack,restart=restart,
        full_frame_min_cycles=min(s['cycles'] for s in samples if not s['test_mode']),
        full_frame_max_cycles=max(s['cycles'] for s in samples if not s['test_mode']),
        motion_max_cycles=max(s['motion_cycles'] for s in samples if not s['test_mode']),
        test_batch_max_cycles=max(s['motion_cycles'] for s in samples if s['test_mode']),
        core_sha256=sha(a.core),core_version=fe.version,
        physical_gba_tested=False,original_bios_tested=False,shared_arm_cases=True)
    Path('analysis/gba/g08-gba-verification.json').write_text(json.dumps(report,indent=2)+'\n')
    fe.core.retro_unload_game();fe.core.retro_deinit()
    print('G08 ARM: shared collision suite, bilingual replay/maps, live wall stop, release, guards/reset passed')

if __name__=='__main__':main()
