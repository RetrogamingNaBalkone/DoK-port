"""N21P host coverage of actual dirty-map platform code with a RAM DMA stub."""
import json
import os
import hashlib
from pathlib import Path
from check_items import ROOT, artifact, run


def main():
    target = ROOT / 'build/nds/n21p/host'
    evidence = ROOT / 'research/evidence/nds/n21p'
    target.mkdir(parents=True, exist_ok=True)
    evidence.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(ROOT / 'build/zig-global-cache'),
               ZIG_LOCAL_CACHE_DIR=str(ROOT / 'build/zig-local-cache'))
    zig = Path.home() / 'AppData/Local/Programs/Zig/0.13.0/zig.exe'
    sources = ['ports/nds/platform/map/runtime.c', 'ports/nds/ui/map_control.c',
               'ports/nds/ui/map_raster.c', 'ports/nds/tests/test_map_runtime.c']
    common = [zig, 'cc', '-std=c11', '-pedantic-errors', '-Wall', '-Wextra', '-Werror',
              '-O2', '-DN21P_HOST_TEST=1', '-I', 'ports/nds/tests/map_runtime_stub', '-I', 'ports/nds/include', *sources]
    results = []
    for audit in (0, 1):
        exe = target / f'map-runtime-{audit}.exe'
        run([*common, f'-DN21P_AUDIT={audit}', '-o', exe], env=env)
        output = run([exe])
        assert 'checks=16948549' in output and 'draws=44 skips=164 deferred=111 PASS' in output
        results.append(dict(audit=audit, output=output.strip(), artifact=artifact(exe)))
    linux = target / 'map-runtime-linux'
    run([*common, '-DN21P_AUDIT=0', '-target', 'x86_64-linux-musl', '-static', '-o', linux], env=env)
    from map.original_map import original_map_result
    maps = [bytearray([32]) * 4096 for _ in range(3)]
    maps[0][10] = 1
    maps[2][10] = 4
    snapshot = dict(width=64, length=64, maps=maps, visited=bytes(4096), map_available=0,
                    objects=[dict(glyph=32, class_id=0, marker=86),
                             dict(glyph=1, class_id=1, marker=2),
                             dict(glyph=4, class_id=0, marker=12)],
                    hero=dict(x=10, z=0, heading=90))
    native_rows = []
    for language in ('ru', 'en'):
        before, post = original_map_result(language, snapshot, 3, -32, 0)
        after, post2 = original_map_result(language, dict(snapshot, visited=post), 3, -32, 0)
        changed = sum(a != b for a, b in zip(before, after))
        assert before[448] == 86 and after[448] == 12 and post[10] == 1 and post == post2 and changed == 13
        native_rows.append(dict(language=language, first_pixel=86, followup_pixel=12,
                                changed_pixels=changed, class1_post=1,
                                first_sha256=hashlib.sha256(before).hexdigest(),
                                followup_sha256=hashlib.sha256(after).hexdigest(),
                                export_manifest=artifact(ROOT / 'research/evidence/automap' / language / 'manifest.json')))
    report = dict(task='N21P', passed=True, checks_per_mode=16948549, modes=results,
                  native_followup_vectors=native_rows,
                  linux_execution=False, linux_artifact=artifact(linux),
                  scope=['actual production runtime with deterministic RAM/DMA device',
                         'source class1 PRE/POST order, one required FCMap followup, and writes during skipped raster',
                         'stable displayed planes, glyphs, visited, pixels during deferred updates',
                         'idle no raster/DMA despite elapsed refresh deadlines',
                         '120 dynamic samples per second aggregated to 12Hz',
                         'immediate drag/zoom/button highlight including saturation and release',
                         'language and explicit frontend invalidation',
                         'pause held-touch absorption, live ABI frame, display frame, profile stages',
                         'normal verified counter zero, audit counter equals actual DMA uploads',
                         'split prepare leaves VRAM/DMA/committed counters unchanged and snapshot probe odd',
                         'transfer does DMA once, preserves odd lock and uncommitted counters; expensive audit occurs only in later commit',
                         'commit transfers if needed, closes lock, publishes counters; repeated transfer/commit is inert',
                         'stage7 sums actual prepare+transfer+commit CPU and excludes simulated waits and GPU gap'],
                  exact_word_comparison=dict(alignments='all16 source/destination alignment pairs modulo4',
                      lengths='0..128',differences='every single byte inside each length; outside-tail differences ignored',
                      class1='all4096 width/length pairs1..64;0..5 class1 glyphs, including0/1/0x80/0x81/0xff; exact writes and untouched inactive cells'),
                  sources=[artifact(ROOT / p) for p in [*sources,
                      'ports/nds/platform/map/runtime.h', 'ports/nds/include/dok/map_raster.h',
                      'ports/nds/include/dok/map_control.h', 'ports/nds/tests/map_runtime_stub/nds.h',
                      'ports/nds/tools/map/original_map.py',
                      'ports/nds/tools/check_map_dirty.py']])
    (evidence / 'host-dirty-map.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
    print(json.dumps(dict(passed=True, checks=33897098, native_followup_vectors=2,
                          report='research/evidence/nds/n21p/host-dirty-map.json')))


if __name__ == '__main__':
    main()
