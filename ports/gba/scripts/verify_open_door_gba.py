"""Paired mGBA check against the frozen pre-fix ROM, not a shared-C oracle.

The old raster at a deliberately overrun crop phase supplies the completely
transparent panel reference. Closed and intermediate phases must remain exact.
"""

import sys as _layout_sys
from pathlib import Path as _LayoutPath
_layout_root = _LayoutPath(__file__).resolve().parents[3]
for _layout_dir in ("scripts", "tools/originals", "tools/reports", "ports/gba/scripts"):
    _layout_sys.path.append(str(_layout_root / _layout_dir))

import argparse
import hashlib
import json
import os
from pathlib import Path
import struct

from verify_gba import sha
from verify_sprites_gba import load_json
from verify_ui_gba import UIProbe


def scene(raw):
    return bytes(raw[y*240+x] for y in range(144) for x in range(240)
                 if not (y < 14 and (x < 80 or x >= 160))
                 and not (y >= 128 and x >= 192))


def captures(core, build, baseline):
    p = UIProbe(core, build)
    result, screens = {}, []
    try:
        p.wait_ui(lambda u: u['draws'] >= 2)
        p.freeze()
        p.command(1, 0)
        model = load_json('research/evidence/runtime/ru/LEVEL1-runtime.json')
        definitions = {o['glyph']: o for o in model['objects']}
        world = p.symbols['g_render_world']
        maps = p.frontend.read(world+17472, 8192)
        for language in (0, 1):
            p.language(language)
            for glyph in b'adg':
                index = next(i for i, o in enumerate(model['objects']) if o['glyph'] == glyph)
                found = None
                for cell, value in enumerate(maps[4096:]):
                    if value != glyph:
                        continue
                    for delta, heading in ((-64, 0), (64, 180), (-1, 270), (1, 90)):
                        neighbor = cell+delta
                        if (0 <= neighbor < 4096 and maps[neighbor] == 32
                                and definitions[maps[4096+neighbor]]['flags'] & 16):
                            found = neighbor, heading
                            break
                    if found:
                        break
                assert found, glyph
                neighbor, heading = found
                address = world+4+36*index
                p.write(address+34, struct.pack('<h', 30))
                for angle in (0, 20):
                    p.camera(0, neighbor % 64*480+240, -300,
                             neighbor // 64*480+240, (heading+angle) % 360)
                    # 32 * (96 // 30) == 96: independently all-zero old crop.
                    # Height128/192 clear at32 as well. No mutation of RES.
                    for phase in ((0, 13, 29, 32) if baseline else (0, 13, 29)):
                        p.write(address+2, struct.pack('<h', phase))
                        p.command(1, 0)
                        raw, _ = p.frame()
                        key = language, glyph, angle, phase
                        result[key] = scene(raw)
                        if glyph == ord('d') and phase == 29:
                            screens.append(p.screen(f'door-{language}-{angle}-open'))
                p.write(address+2, struct.pack('<h', 0))
                p.write(address+34, struct.pack('<h', 1))
    finally:
        p.close()
    return result, screens


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline-build', type=Path, default=Path('build/gba/g17-ui'))
    parser.add_argument('--build-dir', type=Path, default=Path('build/gba/level1-fixes'))
    parser.add_argument('--report', type=Path, default=Path('analysis/gba/level1-door-fixes.json'))
    parser.add_argument('--core', type=Path, default=Path(os.environ['LOCALAPPDATA']) /
                        'Programs/mGBA/libretro-20261024/mgba_libretro.dll')
    args = parser.parse_args()
    originals = {p.as_posix(): sha(p) for p in Path('originals').rglob('*') if p.is_file()}
    before, before_screens = captures(args.core, args.baseline_build, True)
    after, after_screens = captures(args.core, args.build_dir, False)
    cases = []
    for language in (0, 1):
        for glyph in b'adg':
            for angle in (0, 20):
                key = language, glyph, angle
                for phase in (0, 13):
                    assert before[key+(phase,)] == after[key+(phase,)], (key, phase)
                empty = before[key+(32,)]
                assert after[key+(29,)] == empty, ('open panel occludes scene', key)
                changed = sum(a != b for a, b in zip(before[key+(29,)], empty))
                assert changed > 0, ('baseline did not reproduce residual strip', key)
                cases.append(dict(language=language, glyph=glyph, angle=angle,
                                  removed_strip_pixels=changed,
                                  closed_and_middle_pixel_exact=True,
                                  open_matches_old_zero_crop=True,
                                  open_scene_sha256=hashlib.sha256(empty).hexdigest()))
    assert originals == {p.as_posix(): sha(p) for p in Path('originals').rglob('*') if p.is_file()}
    report = dict(status='passed', rom_sha256=sha(args.build_dir/'dok-diagnostic.gba'),
                  baseline_rom_sha256=sha(args.baseline_build/'dok-diagnostic.gba'),
                  script_sha256=sha(__file__), cases=cases, originals_unchanged=originals,
                  before_screens=before_screens, after_screens=after_screens,
                  physical_gba_tested=False, full_dos_session_tested=False)
    args.report.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print('Open-door paired ROM check: 12 RU/EN/front/oblique cases passed')


if __name__ == '__main__':
    main()
