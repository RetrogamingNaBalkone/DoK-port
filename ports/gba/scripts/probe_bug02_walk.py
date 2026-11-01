"""Explore BUG02 room with real keypad movement and actual mGBA PPU captures.

Only starting pose and survival HP/armor are fixtures. Production movement,
collision, AI, animation and combat run normally. Contact sheets are inspection
evidence, not an automatic assertion that BUG02 is present or absent.
"""
import argparse
import json
import os
from pathlib import Path
import struct

from profile_game_gba import GameProbe, freeze
from verify_ai_gba import actor_values
from verify_combat_gba import actor_pose
from verify_gba import VIDEO, png, sha

ROOT = Path(__file__).resolve().parents[3]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--build-dir', type=Path, default=Path('build/gba/g21-bat-fixed/hardware/hw_v128_vblank_only'))
    ap.add_argument('--out', type=Path, default=Path('build/gba/bug02-walk'))
    ap.add_argument('--cell', type=int, nargs=2, default=(21, 48))
    ap.add_argument('--heading', type=int, default=45)
    ap.add_argument('--phase', type=int, nargs=2, default=(240, 240))
    ap.add_argument('--local', action='store_true')
    args = ap.parse_args()
    os.chdir(ROOT)
    os.environ['DOK_PROBE_FAST_VIDEO'] = '1'
    args.out.mkdir(parents=True, exist_ok=True)
    core = Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll'
    model = json.loads(Path('research/evidence/runtime/ru/LEVEL1-runtime.json').read_text())
    p = GameProbe(core, args.build_dir)
    rows, tiles, sheet, ranked = [], [], 0, []

    def save_sheet():
        nonlocal sheet
        if not tiles:
            return
        width, height = 240*4, 160*((len(tiles)+3)//4)
        rgb = bytearray(width*height*3)
        for i, tile in enumerate(tiles):
            for y in range(160):
                dst = ((i//4*160+y)*width+i%4*240)*3
                rgb[dst:dst+720] = tile[y*720:(y+1)*720]
        png(args.out/f'sheet-{sheet:02}.png', width, height, rgb)
        sheet += 1
        tiles.clear()

    try:
        p.video_callback = VIDEO(p.frontend.video)
        p.frontend.core.retro_set_video_refresh(p.video_callback)
        p.until(lambda d: d['draws'] >= 1)
        p.write(p.symbols['g_camera_capture_hold'], struct.pack('<I', 1))
        p.until(lambda d: d['draws'] >= 4 and d['phase'] == 2)
        freeze(p, True)
        actor_pose(p, model, model['player_index']-1,
                   args.cell[0]*480+args.phase[0], args.cell[1]*480+args.phase[1], args.heading)
        player = p.player()
        player[0] = player[2] = 32767
        p.set_player(player)
        # Camera fixture mode must be off: production camera follows actor.
        assert 'test_camera' in p.symbols
        p.write(p.symbols['test_camera'], struct.pack('<I', 0))
        # Retain the scanout capture hold (adds VBlank waiting). Simulation
        # still advances from real time; this run makes no FPS claim.
        freeze(p, False)
        route = [('turn-left', (6,), 120), ('forward', (4,), 40),
                 ('forward-right', (4, 7), 80), ('strafe-left', (10,), 40),
                 ('turn-right', (7,), 120), ('back-left', (5, 6), 80),
                 ('strafe-right', (11,), 40), ('forward-left', (4, 6), 80)]
        if args.local:
            route = [('settle', (), 20)] + [('forward', (4,), 6), ('turn-left', (6,), 4),
                      ('back', (5,), 6), ('turn-right', (7,), 4),
                      ('strafe-left', (10,), 6), ('strafe-right', (11,), 6)]*6
        for label, buttons, count in route:
            for step in range(count):
                before = p.diag()['draws']
                d = p.until(lambda d: d['draws'] > before and d['phase'] == 2, buttons=buttons)
                actor = actor_values(p, model, model['player_index']-1)
                row = dict(index=len(rows), segment=label, buttons=buttons,
                           camera=[d['player_x'], d['player_y'], d['player_z']],
                           actor=[actor['x'], actor['y'], actor['z'], actor['heading']],
                           draws=d['draws'], present_vblank=d['present_vblank'])
                if True:
                    # At phase2 rendering has completed; two scanouts expose
                    # that frame. Live motion can progress during those scans.
                    p.frontend.run(2, buttons)
                    p.frontend.decode_video(p.frontend.last_video_raw)
                    row['scanout_draws'] = p.diag()['draws']
                    rgb = p.frontend.rgb
                    # Inspection ranking only: central ceiling darker than
                    # adjacent strips. Texture and occlusion can also rank high.
                    bands = []
                    for left in range(60, 161, 10):
                        bands.append(sum(rgb[(y*240+x)*3+1]
                                         for y in range(12, 32) for x in range(left, left+10)))
                    scores = [((bands[k-2]+bands[k+2])/2-bands[k], k)
                              for k in range(2, len(bands)-2)]
                    row['dark_band_score'], row['dark_band_index'] = max(scores)
                    ranked.append((row['dark_band_score'], len(rows), rgb))
                    if len(rows) % 4 == 0:
                        row['sheet'], row['tile'] = sheet, len(tiles)
                        tiles.append(rgb)
                        if len(tiles) == 24:
                            save_sheet()
                rows.append(row)
            print(label, rows[-1]['actor'], flush=True)
        save_sheet()
        selected = []
        for score, index, rgb in sorted(ranked, reverse=True):
            if any(abs(index-prior) < 5 for prior in selected):
                continue
            selected.append(index)
            tiles.append(rgb)
            png(args.out/f'candidate-{index:03}.png', 240, 160, rgb)
            if len(tiles) == 24:
                break
        save_sheet()
        result = dict(rom_sha256=sha(p.rom), script_sha256=sha(__file__), core_sha256=sha(core),
                      initial_cell=args.cell, initial_phase=args.phase, initial_heading=args.heading, rows=rows, candidates=selected,
                      scope=__doc__, physical_gba_tested=False)
        (args.out/'walk.json').write_text(json.dumps(result, indent=2)+'\n')
    finally:
        p.close()


if __name__ == '__main__':
    main()
