"""Final main-path attribution and offline sparse-column format feasibility.

The optional profile ROM is diagnostic. This does not implement a sparse-post
renderer, publish a Performance ROM, or infer FPS from instrumented CPU time.
"""
import argparse
import json
import os
from pathlib import Path
import struct

from research_gba_fps import GameProbe, ROOT, freeze
from verify_gba import VIDEO, sha
from verify_renderer_gba import stack_usage


CONTROL = '2775bdd0caaac64b4fd99e4c6d675587fad2da9c795468dc52efacb5d53edaab'
CASES = [(1, 520, 225), (1, 3640, 225), (2, 1592, 135),
         (2, 2871, 225), (3, 1544, 315)]


def source_posts(folder):
    """Count an exact auxiliary run format for banked post-LIGHT columns.

    Four-byte column offsets plus uint16 count and (uint16 start,end) pairs.
    Original prelit bytes remain available for the existing samplers. Shared
    identical (height, colors) columns share their run records. No timing model.
    """
    package = ROOT / 'build/gba/assets/assets.bin'
    data = package.read_bytes()
    u32 = lambda at: struct.unpack_from('<I', data, at)[0]
    directory, views, count = u32(24), u32(28), u32(20)
    offsets = (folder / 'exact-offsets.bin').read_bytes()
    bank = (folder / 'prelit.bin').read_bytes()
    selected = set()
    for view in range(6):
        for rid in range(1, count + 1):
            at = struct.unpack_from('<I', offsets, ((view*(count+1)+rid)*16)*4)[0]
            if not at:
                continue
            entry = directory + (rid-1)*32 + 16 + (view%2)*8
            start, size = struct.unpack_from('<II', data, entry)
            width, height = struct.unpack_from('<HH', data, start)
            assert size == 18 + width*height
            for light in range(16):
                base = struct.unpack_from('<I', offsets, ((view*(count+1)+rid)*16+light)*4)[0]
                assert base and base + width*height <= len(bank)
                selected.add((base, width, height))
    unique = {}
    columns = zero = solid = mixed = samples = transparent = runs = 0
    for base, width, height in sorted(selected):
        for x in range(width):
            column = bank[base+x*height:base+(x+1)*height]
            spans = []
            start = None
            for y, color in enumerate(column):
                if color and start is None:
                    start = y
                elif not color and start is not None:
                    spans.append((start, y)); start = None
            if start is not None:
                spans.append((start, height))
            restored = bytearray(height)
            for first, last in spans:
                restored[first:last] = column[first:last]
            assert restored == column
            unique[(height, column)] = spans
            columns += 1; samples += height
            transparent += column.count(0); runs += len(spans)
            zero += not spans; solid += len(spans) == 1 and spans[0] == (0, height)
            mixed += bool(spans) and not (len(spans) == 1 and spans[0] == (0, height))
    return dict(package_sha256=sha(package), tables_sha256=sha(folder/'exact-offsets.bin'),
                bank_sha256=sha(folder/'prelit.bin'), bank_bytes=len(bank),
                bank_images=len(selected), columns=columns, source_texels=samples,
                zero_columns=zero, solid_columns=solid, mixed_columns=mixed,
                zero_texels=transparent, nonzero_runs=runs, unique_columns=len(unique),
                auxiliary_bytes=columns*4+sum(2+4*len(v) for v in unique.values()),
                scope='All banked source columns/all16 LIGHT rows, deduplicated bank images. '
                      'Not just runtime masked images; no visibility, screen-area weighting, '
                      'minification/coverage costs or native sparse-post timing. '
                      'Exact source reconstruction only; no FPS or speedup claim.')


def profile(folder):
    core = Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll'
    p = GameProbe(core, folder); rows = []
    try:
        p.video_callback = VIDEO(p.frontend.video)
        p.frontend.core.retro_set_video_refresh(p.video_callback)
        p.until(lambda d: d['draws'] >= 1)
        p.write(p.symbols['g_camera_capture_hold'], struct.pack('<I', 1))
        p.until(lambda d: d['draws'] >= 4 and d['phase'] == 2)
        freeze(p, True)
        p.write(p.symbols['g_render_work'], struct.pack('<I', 1))
        previous_level = 0
        for level, cell, heading in CASES:
            if level != previous_level:
                p.camera(level%3, 15120, -300, 22800, 90)
            previous_level = level
            p.camera(level-1, cell%64*480+240, -300, cell//64*480+240, heading)
            for _ in range(6):
                d = p.present()
            assert d['phase'] == 2 and d['guard_ok'] == 1
            work = list(p.words('g_render_work', 5))
            face = list(p.words('g_hw_face_class_work', 4))
            stages = list(p.words('g_render_completed_stages', 7))
            assert work[0] == 1 and face[2] + face[3] == work[3]
            rows.append(dict(level=level, cell=cell, heading=heading,
                instrumented_cpu_cycles=d['last_cycles'], work=work, face_class_work=face,
                attributed_cycles=dict(masked_faces=face[0]*64, opaque_faces=face[1]*64,
                                       horizontal_surfaces=work[2]*64),
                stages=dict(zip(('logic','resources','geometry','sort','raster','ui','copy'),
                                [stages[0]]+[b-a for a,b in zip(stages, stages[1:])]))))
            print(level, cell, rows[-1]['attributed_cycles'], flush=True)
        return dict(rom_sha256=sha(p.rom), core_sha256=sha(core), rows=rows,
                    stack=stack_usage(p.frontend), forced_pcm=False,
                    scope='Completed held draws, frozen simulation, cold world reset after freeze. '
                          'Face timers include setup/clipping/coverage/texels, not alpha tests alone. '
                          'Draw orchestration moved to ROM to fit instrumentation. Timer overhead '
                          'included; no forced PCM; not production FPS or an additive budget of2775.')
    finally:
        p.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', action='store_true')
    parser.add_argument('--out', type=Path, default=Path('research/experiments/g20a-final-costs'))
    parser.add_argument('--report', type=Path, default=Path('analysis/g20a-final-costs.json'))
    a = parser.parse_args(); os.chdir(ROOT)
    os.environ['DOK_PROBE_FAST_VIDEO'] = '1'
    if a.build:
        import prototype_gba_bsp as builder
        os.environ['ZIG_GLOBAL_CACHE_DIR'] = str(ROOT/'build/zig-cache')
        builder.VARIANTS['hw_v128_final_class_profile'] = (
            tuple(d for d in builder.VARIANTS['hw_v128_entity_rect'] if d != 'HW_EDGE_RAM_CALL')
            + ('HW_FACE_CLASS_PROFILE',))
        builder.build(Path('build/select-menu-fix'), a.out,
                      ['hw_v128_entity_rect','hw_v128_final_class_profile'])
    assert sha(a.out/'hw_v128_entity_rect/dok-diagnostic.gba') == CONTROL
    result = dict(script_sha256=sha(__file__), control_rom_sha256=CONTROL,
                  manifest_sha256=sha(a.out/'prototype-build.json'),
                  source_post_feasibility=source_posts(a.out),
                  profile=profile(a.out/'hw_v128_final_class_profile'),
                  sparse_post_disposition='Deferred, not a native rejected architecture. Source '
                      'reconstruction/storage feasibility does not establish a full-frame gain. '
                      'Zero-span bitmap and empty-column native alternatives already failed overall ROI. '
                      'A full sparse-run sampler remains unmeasured future kernel research, '
                      'not a prerequisite for the current remaining-cost handoff.')
    a.report.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print('Report', a.report, flush=True)


if __name__ == '__main__':
    main()
