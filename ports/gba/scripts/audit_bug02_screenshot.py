"""Reproduce the supplied BUG02 ceiling strip and isolate texture contribution.

Actual mGBA PPU. Start pose is a fixture; AI runs for20 draws, then freezes.
The flat-texture diagnostic changes only this emulator instance's ROM bank in
memory and restores it. No cartridge file, original sample or runtime is edited.
This is not a whole-DOS-frame equivalence test or a BUG02 acceptance decision.
"""
import argparse
import json
import os
from pathlib import Path
import struct

from locate_bug02_screenshot import pixels
from profile_game_gba import GameProbe, freeze
from verify_combat_gba import actor_pose
from verify_gba import VIDEO, sha, png
from verify_sprites_gba import original_animations

ROOT = Path(__file__).resolve().parents[3]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--build-dir', type=Path, default=Path('build/gba/g21-bat-fixed/hardware/hw_v128_vblank_only'))
    ap.add_argument('--out', type=Path, default=Path('build/gba/bug02-reproduced'))
    ap.add_argument('--source', type=Path, default=Path('research/evidence/gba/bug02-20261101/mGBA_yPe5zwSUkA.png'))
    args = ap.parse_args()
    os.chdir(ROOT)
    os.environ['DOK_PROBE_FAST_VIDEO'] = '1'
    args.out.mkdir(parents=True, exist_ok=True)
    model = json.loads(Path('research/evidence/runtime/ru/LEVEL1-runtime.json').read_text())
    original, source = original_animations(0)
    reference = pixels(args.source)
    key = next(f['hex'] for f in model['objects'][68]['fields'] if f['offset'] == 86)
    raw = original[key]
    width, height = struct.unpack_from('<HH', raw)
    palette = original[model['palette']]
    rgb = bytes((palette[c*3+k] >> 1)*255//31 for c in raw[18:18+width*height] for k in range(3))
    png(args.out/'original-texture.png', width, height, rgb)
    core = Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll'
    p = GameProbe(core, args.build_dir)
    rom_pin = sha(p.rom)
    backups = {}

    def capture(name):
        p.present()
        p.frontend.run(2)
        p.frontend.decode_video(p.frontend.last_video_raw)
        p.frontend.screenshot(args.out/name)
        return p.frontend.colors555()

    try:
        p.video_callback = VIDEO(p.frontend.video)
        p.frontend.core.retro_set_video_refresh(p.video_callback)
        p.until(lambda d: d['draws'] >= 1)
        p.write(p.symbols['g_camera_capture_hold'], struct.pack('<I', 1))
        p.until(lambda d: d['draws'] >= 4 and d['phase'] == 2)
        freeze(p, True)
        actor_pose(p, model, model['player_index']-1, 10153, 23497, 22)
        p.write(p.symbols['test_camera'], struct.pack('<I', 0))
        freeze(p, False)
        for _ in range(20):
            p.present()
        freeze(p, True)
        actual = capture('reproduced.png')
        region = [(x, y) for y in range(38) for x in range(110, 145)]
        to555 = lambda rgb: sum(round(c*31/255) << (k*5) for k, c in enumerate(rgb))
        assert all(actual[y*240+x] == to555(reference[x, y]) for x, y in region)
        floor = [(x, y) for y in range(100, 140, 3) for x in range(20, 220, 3) if not 98 <= x < 146]
        assert all(actual[y*240+x] == to555(reference[x, y]) for x, y in floor)
        vram = p.frontend.read(0x06000000, 98304)
        palette555 = struct.unpack('<256H', p.frontend.read(0x05000000, 512))
        front = p.words('hw_bg_front', 1)[0]
        active = p.frontend.read(p.symbols['hw_bg_frames']+front*2900, 2900)
        trace = []
        checked = 0
        for y in range(30):
            pa, _, pc, _, rx, ry = struct.unpack_from('<4h2i', active, y*16)
            control = struct.unpack_from('<H', active, 2576+y*2)[0]
            assert control in (0xb682, 0xb882)  # Near/mid ceiling, uniform across X.
            for x in range(110, 145):
                tx, ty = ((rx+pa*x) >> 8) & 511, ((ry+pc*x) >> 8) & 511
                tile = vram[((control >> 8) & 31)*2048+(ty//8)*64+tx//8]
                index = vram[tile*64+(ty & 7)*8+(tx & 7)]
                assert actual[y*240+x] == palette555[index]
                checked += 1
                if y == 8 and x in (110, 120, 130, 140):
                    trace.append(dict(x=x, y=y, control=control, tile=tile, palette_index=index,
                                      texture_uv=[((tile-17) % 4)*8+(tx & 7), ((tile-17)//4)*8+(ty & 7)],
                                      actual555=actual[y*240+x]))
        # CPU camera fixtures now isolate adjacent headings with actors frozen.
        for heading in (21, 22, 23):
            p.camera(0, 10153, -300, 23497, heading)
            capture(f'heading-{heading}.png')
        p.camera(0, 10153, -300, 23497, 22)
        before = capture('before-flat.png')
        offsets = p.frontend.read(p.symbols['dok_hw_bg_offsets'], 2592*4)
        # Mat1, phase0, all3 bands. Change bank source, then force an emulated
        # DMA upload so mGBA invalidates its decoded-tile cache normally.
        for band in range(3):
            offset = struct.unpack_from('<I', offsets, (18*3+band)*4)[0]
            address = p.symbols['dok_hw_bg_bank']+offset
            backups[address] = p.frontend.read(address, 1024)
            p.write(address, bytes([128])*1024)
        p.write(p.symbols['hw_bg_phases'], bytes([255])*8)
        flat = capture('flat-texture.png')
        assert all(flat[y*240+x] == palette555[128] for y in range(30) for x in range(110, 145))
        for address, data in backups.items():
            p.write(address, data)
        backups.clear()
        p.write(p.symbols['hw_bg_phases'], bytes([255])*8)
        restored = capture('restored.png')
        assert before == restored
        assert sha(p.rom) == rom_pin and sha(Path(source['path'])) == source['sha256']
        result = dict(status='reproduced-and-texture-isolated', camera=[10153, -300, 23497, 22],
                      level=1, cell=[21, 48], source_sha256=sha(args.source), rom_sha256=rom_pin,
                      core_sha256=sha(core), script_sha256=sha(__file__), original_res=source,
                      ceiling_source_exact_pixels=len(region), floor_source_exact_pixels=len(floor),
                      ceiling_gpu_exact_pixels=checked, ceiling_light_rows=[0, 7], texture_name_hex=key,
                      texture_source_size=[width, height], texture_gpu_size=[32, 32], trace=trace,
                      flat_test_uniform_pixels=1050, restored_ppu_exact=True, physical_gba_tested=False,
                      original_DOS_projection_equivalence=False, scope=__doc__)
        (args.out/'reproduction.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
        print('Reproduced:1330 ceiling /714 floor source pixels;1050 GPU pixels; flat/restore exact.')
    finally:
        for address, data in backups.items():
            p.write(address, data)
        p.close()


if __name__ == '__main__':
    main()
