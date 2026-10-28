"""G06A full-turn/multi-level camera harness; actual ARM cycles, no FPS inference.

Uses the production renderer on host as a regression oracle, with explicit
even-column duplication for the accepted 120-sample scene mode.
This does not establish whole-DOS pixel equivalence.
"""

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
from verify_gba import Frontend, sha
from verify_renderer_gba import NAMES, stack_usage, freeze_visual
from verify_renderer_host import write_frame_png

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--build-dir', type=Path, default=Path('build/gba/g06a-fast'))
p.add_argument('--assets', type=Path, default=Path('build/gba/assets/assets.bin'))
p.add_argument('--tool', type=Path, default=Path('build/gba/host/dok-render-reference.exe'))
p.add_argument('--core', type=Path, default=Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll')
p.add_argument('--step', type=int, default=1)
p.add_argument('--half-columns',action='store_true',help='Compare 120-sample scene to duplicated even columns of the 240-sample oracle')
p.add_argument('--report', type=Path, default=Path('analysis/gba/render-early-benchmark.json'))
a = p.parse_args()
assert 1 <= a.step <= 90
build = a.build_dir
out = build/('camera-check' if a.step==1 else f'camera-check-step{a.step}')
out.mkdir(exist_ok=True)
_, symbols, sections = elf_info(build/'dok-diagnostic.elf')
meta = json.loads((build/'build-report.json').read_text())
assert meta['rom_sha256'] == sha(build/'dok-diagnostic.gba')
f = Frontend(a.core.resolve(), build)
f.load(build/'dok-diagnostic.gba')

def diag():
    return dict(zip(NAMES, struct.unpack('<32I', f.read(symbols['g_render_diag'], 128))))

def until(predicate, limit=6000):
    for _ in range(limit):
        f.run(1)
        basic = f.diag(symbols['g_diag'])
        if basic['magic'] == 0x444f4b31:
            assert not basic['fault'], ('ARM panic', basic, diag())
        d = diag()
        freeze_visual(f,symbols,d['magic'])
        if predicate(d):
            return d
    raise AssertionError(('Timeout', d))

until(lambda d: d['draws'] >= 2 and d['phase'] == 2)
ct.memmove(f.pointer(symbols['g_camera_capture_hold'],4),struct.pack('<I',1),4)
results, worlds, failures, language_checks = [], {}, [], []
request = 0
for level in range(3):
    # Startup pose from the validated common G03 runtime, not DOS EN placement.
    cmd = [str(a.tool), str(a.assets), str(level+1), 'ru', str(out/'host.idx'), str(out/'host.pal')]
    initial = json.loads(subprocess.check_output(cmd, text=True))
    c = initial['camera']
    poses = [('initial', c['x'], c['y'], c['z'])]
    # Controlled nearby offsets, grazing a cell boundary and map corners.
    poses += [('near-x', (c['x']//480)*480+1, c['y'], c['z']),
              ('near-z', c['x'], c['y'], (c['z']//480)*480+1),
              ('offset', max(0,c['x']-960), c['y'], max(0,c['z']-960)),
              ('boundary-low', 1, -300, 1), ('boundary-high', 30719, -300, 30719)]
    for pose,x,y,z in poses:
        headings = list(range(0,360,a.step if pose == 'initial' else max(a.step,15)))
        if pose == 'initial': headings += [-721,-360,-1,360,721]
        for heading in headings:
            camera = [x,y,z,heading]
            host = subprocess.run(cmd+list(map(str,camera)), text=True, capture_output=True)
            if host.returncode:
                failures.append(dict(level=level+1,pose=pose,camera=camera,error=host.stderr.strip()))
                continue
            h = json.loads(host.stdout)
            expected = (out/'host.idx').read_bytes()
            if a.half_columns:
                expected=bytes(value for value in expected[::2] for _ in range(2))
            request += 1
            ct.memmove(f.pointer(symbols['g_camera_probe']+8,20), struct.pack('<I4i',level,*camera),20)
            ct.memmove(f.pointer(symbols['g_camera_probe'],4),struct.pack('<I',request),4)
            before = diag()['draws']
            d = until(lambda d: struct.unpack('<I',f.read(symbols['g_camera_probe']+4,4))[0] == request and d['draws'] >= before+2 and d['phase'] == 2)
            world = f.read(symbols['g_render_world'],33864)
            if level not in worlds: worlds[level] = world
            assert world == worlds[level], 'Camera changed runtime'
            assert d['guard_ok'] == 1
            assert f.read(symbols['g_render_frame'],4) == struct.pack('<I',0xd04fcafe)
            assert f.read(symbols['g_render_frame']+38404,4) == struct.pack('<I',0xd04fcafe)
            page = d['visible_page']
            actual = f.read(0x06000000+page*0xa000,38400)
            # HUD covers two boxes. The bottom16 and these boxes are caller UI.
            for row in range(144):
                lo,hi = (56,192) if row<12 else (0,240)
                assert actual[row*240+lo:row*240+hi] == expected[row*240+lo:row*240+hi], ('pixels',level,pose,heading,row)
            stages = struct.unpack('<7I',f.read(symbols['g_render_stages'],28))
            result = dict(level=level+1,pose=pose,camera=camera,cycles=d['last_cycles'],
                          stages=dict(zip(('clear_resources','geometry','sort','raster','hud','copy'),
                                          [b-a for a,b in zip(stages,stages[1:])])),
                          faces=h['stats']['face_count'],near_clipped=h['stats']['near_clipped'],
                          reference_pixel_writes=h['stats']['pixels_written'],visible_pixel_writes=d['pixels'],
                          reference_transparent=h['stats']['transparent_pixels'],
                          framebuffer_sha256=hashlib.sha256(actual).hexdigest())
            if heading%90 == 0 or pose != 'initial' and heading == 45:
                path=out/f'L{level+1}-{pose}-{heading}.png'
                write_frame_png(path,actual,(out/'host.pal').read_bytes(),quantized=True)
            results.append(result)
    # RU -> EN -> RU at the level's startup view: unchanged runtime and scene.
    request += 1
    ct.memmove(f.pointer(symbols['g_camera_probe']+8,20),struct.pack('<I4i',level,c['x'],c['y'],c['z'],90),20)
    ct.memmove(f.pointer(symbols['g_camera_probe'],4),struct.pack('<I',request),4)
    before=diag()['draws']
    until(lambda d: d['draws']>=before+2 and d['phase']==2)
    def page_bytes():
        d=diag();return f.read(0x06000000+d['visible_page']*0xa000,38400)
    ru=page_bytes()
    for language in (1,0):
        old=diag()['switches']
        for _ in range(200):
            f.run(1,[2])
            if diag()['switches']>old: break
        assert diag()['switches']==old+1
        before=diag()['draws']
        until(lambda d: d['last_keys']==0 and d['draws']>=before+3 and d['phase']==2)
        assert diag()['language']==language
        assert f.read(symbols['g_render_world'],33864)==worlds[level]
        pixels=page_bytes()
        assert pixels[:34560]==ru[:34560]
        assert (pixels[34560:]!=ru[34560:]) == bool(language)
        if not language: assert pixels==ru
    language_checks.append(dict(level=level+1,ru_en_ru=True,world_equal=True,scene_equal=True))
    print(f'LEVEL{level+1}: {len(results)} ARM/host cameras checked, {len(failures)} rejected',flush=True)

stack = stack_usage(f)
assert stack['untouched_margin_bytes'] >= 512
assert results
assert meta['rom_sha256']==sha(build/'dok-diagnostic.gba'), 'ROM changed during camera verification'
worst = max(results,key=lambda row:row['cycles'])
report = dict(task='G06A',status='failed' if failures else 'passed',
              budget_met=worst['cycles']<=400000,
              performance_status='met' if worst['cycles']<=400000 else 'deferred',
              budget_policy='User decision 2026-10-25: accept 120 horizontal scene samples with full-resolution UI; early excess deferred; full-game optimization G20',
              rom_sha256=sha(build/'dok-diagnostic.gba'),rom_bytes=meta['rom_bytes'],
              ewram_static_bytes=meta['ewram_static_bytes'],sections=sections,
              emulator=f.name,version=f.version,core_sha256=sha(a.core),
              bios='mGBA replacement BIOS',budget=400000,worst=worst,
              max_faces=max(r['faces'] for r in results),rejected=failures,
              checked_cameras=len(results),language_checks=language_checks,host_arm_scene_equal=True,world_unchanged=True,
              horizontal_scene_samples=120 if a.half_columns else 240,
              oracle_transform='duplicate even columns before full-resolution HUD' if a.half_columns else 'none',
              capture_hold='one extra VBlank after presentation, outside CPU timer; camera suite does not measure cadence',
              guards_passed=True,stack=stack,physical_gba_tested=False,
              full_game_performance_verified=False,samples=results)
a.report.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
f.core.retro_unload_game()
f.core.retro_deinit()
print(f"{report['status']}: worst {worst['cycles']} cycles; report {a.report}")
