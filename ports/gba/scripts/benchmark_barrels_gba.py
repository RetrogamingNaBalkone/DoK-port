"""Paired actual PPU/CPU/FPS checks around LEVEL1's copper key and barrel row."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import statistics
import struct
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.append(str(ROOT / 'ports/gba/scripts'))
from research_gba_fps import GameProbe, freeze, collect
from verify_audio_gba import sounds, irq_stack
from verify_gba import VIDEO, sha
from verify_renderer_gba import stack_usage
from verify_sprites_gba import load_json

CELLS = ((42, 54), (43, 55), (44, 55), (46, 56))


def measure(build, core, live_samples, crowded=False):
    p = GameProbe(core, build)
    frozen = []; live = []
    try:
        p.video_callback = VIDEO(p.frontend.video)
        p.frontend.core.retro_set_video_refresh(p.video_callback)
        p.until(lambda d: d['draws'] >= 1)
        p.write(p.symbols['g_camera_capture_hold'], struct.pack('<I', 1))
        p.until(lambda d: d['draws'] >= 4 and d['phase'] == 2)
        freeze(p, True)
        rate = max(sounds()[0], key=lambda r: r['rate'])
        for language in (0, 1):
            p.language(language)
            p.camera(1, 15120, -300, 22800, 90)
            cells=((8,8),(56,56),(2,35),(56,35)) if crowded else CELLS
            for x, z in cells:
                for heading in range(0, 360, 45):
                    p.write(p.symbols['g_camera_capture_hold'], struct.pack('<I', 1))
                    p.present()
                    camera = [x * 480 + 240, -300, z * 480 + 240, heading]
                    p.camera(0, *camera)
                    p.command(6); p.command(2, rate['id'])
                    collect(p, 6)
                    timing = collect(p, 12)
                    assert timing['sound_active_frames'] == 12
                    p.write(p.symbols['g_camera_capture_hold'], struct.pack('<I', 1))
                    d = p.present()
                    coverage = p.frontend.read(p.symbols['coverage_bits'], 120 * 16)
                    p.frontend.run(2)
                    video = p.frontend.last_video_raw
                    frozen.append(dict(language=language, cell=[x, z], heading=heading,
                        camera=camera, cpu_cycles=timing['median'], fps=timing['presented_fps'],
                        video_sha256=hashlib.sha256(video).hexdigest(),
                        coverage_sha256=hashlib.sha256(coverage).hexdigest(),
                        geometry_fnv1a=d['geometry_fnv1a'],
                        stage_medians={k:statistics.median(s['stages'][k] for s in timing['samples'])
                                       for k in timing['samples'][0]['stages']}))
                    if language == 0 and heading == 0 and (x,z) in ((42,54),(44,55)):
                        p.frontend.screenshot(build / f'barrels-{x}-{z}.png')
            if not live_samples:
                continue
            model = load_json('research/evidence/runtime/ru/LEVEL1-runtime.json')
            live_cases=((8,8,225),(56,56,225),(8,8,90)) if crowded else ((42,54,0),(44,55,0),(46,56,90))
            for x,z,heading in live_cases:
                freeze(p, True)
                p.write(p.symbols['g_camera_capture_hold'], struct.pack('<I', 1))
                p.present()
                p.camera(1, 15120, -300, 22800, 90)
                p.camera(0, x*480+240, -300, z*480+240, heading)
                hero = model['objects'][model['player_index']-1]
                actor = p.symbols['g_render_world'] + 9220 + hero['actor_index']*64
                p.write(actor+4, struct.pack('<iii', x*480+240, -300, z*480+240))
                p.write(actor+36, struct.pack('<h', heading))
                player = p.player(); player[0] = player[2] = 32767; p.set_player(player)
                p.command(6); p.command(2, rate['id'])
                p.write(p.symbols['test_camera'], b'\0')
                freeze(p, False)
                collect(p, 6)
                timing = collect(p, live_samples)
                live.append(dict(language=language, cell=[x,z], heading=heading,
                    cpu_cycles=timing['median'], fps=timing['presented_fps'],
                    max_ai_owners=timing['max_ai_owners'], sound_active_frames=timing['sound_active_frames'],
                    motion_steps=timing['motion_steps'], max_motion_debt=timing['max_motion_debt'],
                    audio_underruns=timing['audio']['underruns'], samples=live_samples,
                    player_camera=[p.diag()[k] for k in ('player_x','player_y','player_z')]))
                assert all(s['flow_state']==2 and s['ui_state']==0 for s in timing['samples'])
                freeze(p, True)
                p.write(p.symbols['g_camera_capture_hold'], struct.pack('<I', 1))
                p.present()
        return dict(rom_sha256=sha(p.rom), frozen=frozen, live=live,
                    stack=stack_usage(p.frontend), irq_stack=irq_stack(p))
    finally:
        p.close()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--build-dir', type=Path, required=True)
    ap.add_argument('--reference-build', type=Path, required=True)
    ap.add_argument('--report', type=Path, required=True)
    ap.add_argument('--live-samples', type=int, default=120)
    ap.add_argument('--crowded', action='store_true', help='Use skull/cross and low-ceiling LEVEL1 rooms')
    args = ap.parse_args()
    os.chdir(ROOT)
    os.environ['DOK_PROBE_FAST_VIDEO'] = '1'
    core = Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll'
    before = measure(args.reference_build, core, args.live_samples,args.crowded)
    after = measure(args.build_dir, core, args.live_samples,args.crowded)
    for old,new in zip(before['frozen'],after['frozen']):
        for key in ('language','cell','heading','video_sha256','coverage_sha256','geometry_fnv1a'):
            assert old[key] == new[key], (key,old,new)
    assert len(before['frozen']) == len(after['frozen']) == 64
    ratios = [new['cpu_cycles']/old['cpu_cycles'] for old,new in zip(before['frozen'],after['frozen'])]
    result = dict(status='passed',script_sha256=sha(__file__),core_sha256=sha(core),
                  rom_sha256=after['rom_sha256'],before=before,after=after,
                  cpu_ratio=dict(minimum=min(ratios),median=statistics.median(ratios),maximum=max(ratios)),
                  crowded=args.crowded,
                  scope='64 frozen original-map views/PCM, exact actual PPU/coverage/geometry. '
                        'Live fixtures place the hero beside the barrel row, retain original AI and maps, '
                        'use test HP32767 and stationary input; differing frame times change AI poses. '
                        'Not a full playthrough or physical GBA measurement.')
    args.report.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print('Barrel room: 64 PPU-exact views; CPU ratios',result['cpu_ratio'])
    for old,new in zip(before['live'],after['live']):
        print('live',old['language'],old['cell'],round(old['fps'],2),'->',round(new['fps'],2))


if __name__ == '__main__':
    main()
