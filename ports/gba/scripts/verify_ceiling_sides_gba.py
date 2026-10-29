"""Native edge-plane V bounds and original-texture ceiling rims in actual PPU video."""

import argparse
import ctypes as ct
import json
import os
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[3]
for directory in ('ports/gba/scripts', 'tools/originals'):
    sys.path.append(str(ROOT / directory))

from verify_surfaces import Original, R
from research_gba_fps import GameProbe, freeze
from verify_gba import VIDEO, sha, scene_source_y
from verify_gba_static_cache import Workspace
from verify_gba_hardware_water import color555
from verify_sprites_gba import original_animations


def native_bounds(language):
    """Run the Ghidra-exported constructor and draw; intercept only raster dispatch."""
    original = Original(language)
    c = original.c
    observed = []

    def dispatch(cpu, address, size, unused):
        if address != 0x6fff0:
            return
        sp = cpu.reg_read(R.UC_X86_REG_SP)
        offset, segment = struct.unpack('<HH', cpu.mem_read(0x30000 + sp + 16, 4))
        observed.append(list(struct.unpack('<8i', cpu.mem_read(segment * 16 + offset, 32))))
        ip, cs = struct.unpack('<HH', cpu.mem_read(0x30000 + sp, 4))
        cpu.reg_write(R.UC_X86_REG_SP, sp + 4)
        cpu.reg_write(R.UC_X86_REG_CS, cs)
        cpu.reg_write(R.UC_X86_REG_IP, ip)

    from unicorn import UC_HOOK_CODE
    c.hook_add(UC_HOOK_CODE, dispatch)
    delta = original.delta
    for offset, fmt, values in (
        (0xaa16, 'i', (200,)), (0xaa1e, 'hh', (159, 79)),
        (0xaa22, '4i', (0, 0, 319, 159)), (0xaa32, 'h', (8,)),
        (0xaa3e, '5i', (0, 32768, 240, -300, -2000)),
        (0xaa56, 'HH', (0x2000, 0x3000)),
    ):
        c.mem_write(0x30000 + offset + delta, struct.pack('<' + fmt, *values))
    c.mem_write(0x30000 + (0x934 if language == 'ru' else 0xa44), struct.pack('<h', 4096))
    c.mem_write(0x32000, struct.pack('<H', 0x2100))
    c.mem_write(0x32148, struct.pack('<HH', 0xfff0, 0x6000))
    cases = []
    for height in (96, 192):
        for inset in (10, 400):
            end = height * inset // 960
            c.mem_write(0x41000, struct.pack('<iii', 0, inset - 960, 0))
            c.mem_write(0x44000, struct.pack('<HH', 96, height))
            c.mem_write(0x42000, bytes(0x80))
            original.setup(0x5000)
            args = (0xfff0, 0x5000, 0x2000, 0x4000, 0x1000, 0x4000,
                    inset, 480, 0, 0, 0x4000, 0x4000, 0, 0, end)
            c.mem_write(0x3f000, struct.pack('<15H', *args))
            c.emu_start(0x50000 + (0x6aa if language == 'ru' else 0x6b6), 0x5fff0, count=100000)
            assert bytes(c.mem_read(0x42006, 1))[0] & 1
            assert struct.unpack('<hh', c.mem_read(0x4204b, 4)) == (0, end or height - 1)
            original.setup(0x5000)
            c.mem_write(0x3f000, struct.pack('<4H', 0xfff0, 0x5000, 0x2000, 0x4000))
            c.emu_start(0x50000 + (0xbf5 if language == 'ru' else 0xc0d), 0x5fff0, count=100000)
            assert observed[-1][1::2] == [0, 0, end, end], observed[-1]
            cases.append(dict(language=language,source_height=height,inset=inset,uv=observed[-1]))
    return cases


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--build-dir', type=Path, required=True)
    ap.add_argument('--report', type=Path, required=True)
    args = ap.parse_args()
    os.chdir(ROOT)
    os.environ['DOK_PROBE_FAST_VIDEO'] = '1'
    sources = []
    vectors = []
    for language, sample in (('ru', 'RUS/PKR.EXE'), ('en', 'ENG/PKE.EXE')):
        path = Path('originals/pkr') / sample
        before = sha(path)
        manifest = json.loads((Path('research/evidence/surfaces') / language / 'analysis-manifest.json').read_text(encoding='utf-8-sig'))
        assert before == manifest['sha256_before'] == manifest['sha256_after']
        vectors.extend(native_bounds(language))
        assert sha(path) == before
        sources.append(dict(sample=path.as_posix(),sha256_before=before,sha256_after=sha(path),
                            geometry_sha256=sha(Path('research/evidence/surfaces') / language / 'geometry.bin'),
                            listing_sha256=sha(Path('research/evidence/rendering') / language / 'evidence.asm')))
    core = Path(os.environ['LOCALAPPDATA']) / 'Programs/mGBA/libretro-20261024/mgba_libretro.dll'
    p = GameProbe(core, args.build_dir)
    rows = []
    try:
        p.video_callback = VIDEO(p.frontend.video)
        p.frontend.core.retro_set_video_refresh(p.video_callback)
        p.until(lambda d: d['draws'] >= 1)
        p.write(p.symbols['g_camera_capture_hold'], struct.pack('<I', 1))
        p.until(lambda d: d['draws'] >= 4 and d['phase'] == 2)
        freeze(p, True)
        model = json.loads(Path('research/evidence/runtime/ru/LEVEL1-runtime.json').read_text())
        for language in (0, 1):
            original, origin = original_animations(language)
            p.language(language)
            p.camera(1, 15120, -300, 22800, 90)
            for x in (15120, 15360, 15600):
                p.camera(0, x, -300, 22800, 90)
                images = []
                for warm in (False, True):
                    for _ in range(8 if warm else 2):
                        p.present()
                    w = Workspace.from_buffer_copy(p.frontend.read(p.symbols['g_render_frame'], ct.sizeof(Workspace)))
                    p.frontend.run(2)
                    video = p.frontend.last_video_raw
                    images.append(video)
                    palette = original[model['palette']]
                    light = original[model['light']]
                    checked = 0
                    for f in w.surfaces[:w.surface_count]:
                        obj, plane, cx, cz = f.s[:4]
                        if plane != 2 or cx != 26 or cz not in (46, 47):
                            continue
                        sx = [ct.c_int32(v).value for v in f.n[:4]]
                        top, _, bottom, _ = [ct.c_int32(v).value for v in f.n[4:8]]
                        left, right = sorted((sx[0], sx[1]))
                        depth = f.n[8]
                        assert depth == f.n[9] and 0 <= top < bottom < 128
                        definition = model['objects'][obj]
                        name = next(field['hex'] for field in definition['fields'] if field['offset'] == 0x5e)
                        texture = original[name]
                        width, height = struct.unpack_from('<HH', texture)
                        end = height * 400 // 960  # Original caller's final constructor argument.
                        row = max(0, min(15, (depth - 1920) // 120))
                        pixels = 0
                        for y in range(144):
                            sy = scene_source_y(y, 64)
                            if not top < sy < bottom:
                                continue
                            v = ((sy - top) * ((end << 16) // (bottom - top))) >> 16
                            for screen_x in range((left + 3) & ~1, right - 2, 2):
                                u = (width - 1) * (screen_x - left) // (right - left)
                                index = light[row * 256 + texture[18 + v * width + u]]
                                rgb = palette[index * 3:index * 3 + 3]
                                expected = (rgb[0] >> 1) | ((rgb[1] >> 1) << 5) | ((rgb[2] >> 1) << 10)
                                assert color555(video, p.frontend.pixel_format, y * 240 + screen_x) == expected, (language, x, warm, screen_x, y, v)
                                pixels += 1
                        assert pixels > 200
                        checked += pixels
                    assert checked > 400
                    output = args.build_dir / f'ceiling-rim-{language}-{x}-{int(warm)}.png'
                    p.frontend.screenshot(output)
                    rows.append(dict(language=language,camera=[x,-300,22800,90],warm=warm,ppu_pixels=checked,
                                     screenshot=output.as_posix(),source=origin))
                assert images[0] == images[1], 'Static cache changed the corrected rim'
        result = dict(status='passed',rom_sha256=sha(p.rom),script_sha256=sha(__file__),original_vectors=vectors,
                      original_evidence=sources,ppu_cases=rows,ppu_pixels=sum(r['ppu_pixels'] for r in rows),
                      physical_gba_tested=False,whole_dos_pixel_equivalence=False)
        args.report.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
        print('Native V bounds and original-texture PPU ceiling rims passed:', len(rows), 'cases')
    finally:
        p.close()


if __name__ == '__main__':
    main()
