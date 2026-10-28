"""G07: live GBA buttons, exact-tick ARM replay, release stop and scene capture."""

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
from gba_rom import elf_info
from verify_gba import Frontend, sha
from verify_renderer_gba import NAMES as RENDER_NAMES, stack_usage
from verify_movement import host

NAMES=('magic error steps debt commands last_cycles max_cycles total_cycles '
       'x y z heading direction step accumulator_a accumulator_b command '
       'held pressed released seen quantum step_distance motion_flags remembered_flags').split()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir',type=Path,default=Path('build/gba/g07'))
    parser.add_argument('--core',type=Path,default=Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll')
    args=parser.parse_args();build=args.build_dir
    _,symbols,_=elf_info(build/'dok-diagnostic.elf')
    metadata=json.loads((build/'build-report.json').read_text())
    fe=Frontend(args.core.resolve(),build);fe.load(build/'dok-diagnostic.gba')
    samples=[];captures=[];request=0

    def motion():
        values=list(struct.unpack('<25I',fe.read(symbols['g_motion_diag'],100)))
        for i in list(range(8,17))+[22]:
            if values[i]>=2**31: values[i]-=2**32
        return dict(zip(NAMES,values))

    def render():
        return dict(zip(RENDER_NAMES,struct.unpack('<32I',fe.read(symbols['g_render_diag'],128))))

    def step(buttons=()):
        fe.run(1,buttons)
        d=motion();r=render();basic=fe.diag(symbols['g_diag'])
        if basic['magic']==0x444f4b31: assert not basic['fault'],basic
        if d['magic']==0x4d4f5437: assert not d['error'],d
        if r['draws'] and r['magic']==0x524e4435:
            assert r['status']==0 and r['guard_ok']==1,r
            if not samples or samples[-1]['draws']!=r['draws']:
                samples.append(dict(draws=r['draws'],cycles=r['last_cycles'],vblank=r['present_vblank'],
                                    motion_cycles=d['last_cycles'],steps=d['steps'],heading=d['heading'],x=d['x'],z=d['z']))
        return d

    def until(predicate,buttons=(),limit=1500):
        for _ in range(limit):
            d=step(buttons)
            if d['magic']==0x4d4f5437 and predicate(d): return d
        raise AssertionError(('movement timeout',d,render()))

    def ready():
        d=until(lambda d:render()['draws']>=3)
        assert (d['x'],d['y'],d['z'],d['heading'])==(15120,-300,22800,90),d
        assert d['quantum']==416 and not d['error']
        return d

    def write(address,data):
        ct.memmove(fe.pointer(address,len(data)),data,len(data))

    def capture(name):
        r=render();target=r['draws']+2
        until(lambda d:render()['draws']>=target)
        fe.screenshot(build/f'movement-{name}.png')
        captures.append(dict(name=name,motion=motion(),render=render(),
                             rgb_sha256=hashlib.sha256(fe.rgb).hexdigest()))

    def snapshot(d):
        return [d[k] for k in ('x','y','z','heading','direction','step','accumulator_a','accumulator_b',
                              'command','step_distance','motion_flags','remembered_flags')]

    initial=ready();capture('start')
    write(symbols['g_motion_probe'],struct.pack('<I',1))
    until(lambda d: d['held']==0)
    d=motion()
    expected=[d['x'],d['y'],d['z'],d['heading'],d['direction'],d['step'],d['accumulator_a'],d['accumulator_b'],
              d['command'],-1,d['step_distance'],d['motion_flags'],d['remembered_flags'],0]
    trace=[]
    sequence=[(16,4),(0,4),(32,4),(0x80000,4),(0x100000,4),(4,12),(8,12),(0x2010,3),
              (0,4),(0x180000,3),(48,3),(20,5),(0,5)]
    for commands,count in sequence:
        vectors=[]
        for _ in range(count):
            v=[commands,416,expected[0],expected[1],expected[2],expected[3],5,120,
               expected[6],expected[7],expected[8],expected[11]&0xffffffff,expected[12]&0xffffffff,expected[10],1]
            expected=host([v])[0];vectors.append(v)
        request+=1
        write(symbols['g_motion_probe']+12,struct.pack('<II',commands,count))
        write(symbols['g_motion_probe']+4,struct.pack('<I',request))
        until(lambda d:struct.unpack('<I',fe.read(symbols['g_motion_probe']+8,4))[0]==request)
        d=motion()
        got=snapshot(d)
        want=expected[:9]+expected[10:13]
        want[-2:]=[x&0xffffffff for x in want[-2:]]
        assert got==want,(commands,count,want,got)
        trace.append(dict(commands=commands,count=count,expected=want,actual=got))
    capture('replay')
    # Reset gives the live-button test the original camera and clean adapter.
    fe.core.retro_reset();samples.clear();ready()
    live=[]
    for name,buttons,held,command in (
            ('forward',[4],64,16),('back',[5],128,32),('strafe-left',[10],512,0x80000),
            ('strafe-right',[11],256,0x100000),('turn-left',[6],32,4),('turn-right',[7],16,8),
            ('run-forward',[0,4],66,0x2010),('diagonal',[4,10],576,0x80010),
            ('fire',[8],1,0x200),('use',[3],8,0x800)):
        before=motion();start_steps=before['steps']
        required_steps=8 if name.startswith('turn') else 4
        if name=='use':
            first=until(lambda d:d['held']==held and d['commands']==command,buttons)
            d=until(lambda d:d['steps']>=first['steps']+4,buttons)
            assert not d['commands']&0x800,('use repeated while held',d)
        else:
            d=until(lambda d:d['held']==held and d['steps']>=start_steps+required_steps,buttons)
        if name!='use': assert d['commands']==command,(name,d)
        if name in ('forward','back','strafe-left','strafe-right','run-forward','diagonal'):
            assert (d['x'],d['z'])!=(before['x'],before['z']),(name,before,d)
        if name.startswith('turn'): assert d['heading']!=before['heading'],(name,before,d)
        held_result=d
        released=until(lambda d:d['held']==0 and d['step']==0)
        pose=(released['x'],released['y'],released['z'])
        end_step=released['steps']+5
        stopped=until(lambda d:d['steps']>=end_step)
        assert (stopped['x'],stopped['y'],stopped['z'])==pose,('release drift',name,released,stopped)
        live.append(dict(name=name,held=held_result,released=released,stopped=stopped))
    capture('live')
    assert captures[0]['rgb_sha256']!=captures[-1]['rgb_sha256'],'Live motion did not change the visible scene'
    # Select remains a UI edge; mutable motion state must survive RU/EN.
    stopped=motion();start=render()['switches']
    until(lambda d:render()['switches']>start,[2]);until(lambda d:d['held']==0)
    assert snapshot(motion())==snapshot(stopped)
    for _ in range(10): step([2])
    assert render()['switches']==start+2  # release/repress caused one new edge
    until(lambda d:d['held']==0)
    capture('language')
    stack=stack_usage(fe)
    fe.core.retro_reset()
    ct.memset(fe.pointer(0x02000000,metadata['ewram_static_bytes']),0xa5,metadata['ewram_static_bytes'])
    restart=ready()
    assert struct.unpack('<5I',fe.read(symbols['g_motion_probe'],20))==(0,0,0,0,0)
    report=dict(task='G07',status='passed',rom_sha256=sha(build/'dok-diagnostic.gba'),rom_bytes=(build/'dok-diagnostic.gba').stat().st_size,
                ewram_static_bytes=metadata['ewram_static_bytes'],core_sha256=sha(args.core),core_version=fe.version,
                initial=initial,replay=trace,live_buttons=live,captures=captures,stack=stack,restart=restart,
                samples=samples,full_frame_min_cycles=min(s['cycles'] for s in samples),
                full_frame_max_cycles=max(s['cycles'] for s in samples),
                physical_gba_tested=False,original_bios_tested=False,
                policy='instant translation start/stop/reversal; original turn acceleration',
                replay_scope='same explicit commands and 416-tick steps on host and ARM; original x86 evidence is separate')
    Path('analysis/gba/g07-gba-verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    fe.core.retro_unload_game();fe.core.retro_deinit()
    print('G07 ARM: shared edge suite, exact-tick replay, live buttons, no release drift, language/reset/guards passed')


if __name__=='__main__':main()
