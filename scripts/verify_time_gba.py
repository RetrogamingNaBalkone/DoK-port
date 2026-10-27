"""G06: hardware clock and deterministic fixed steps, standalone or G05 ROM."""
import argparse
import ctypes as ct
import json
import os
from pathlib import Path
import struct
from gba_rom import elf_info
from verify_gba import Frontend, sha
from verify_renderer_gba import start_gameplay

NAMES = 'magic error ticks counter remainder step_now debt steps callbacks polls max_debt last_callback'.split()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--core', type=Path, default=Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll')
    parser.add_argument('--build-dir', type=Path, default=Path('build/gba-time'))
    parser.add_argument('--report', type=Path, default=Path('analysis/g06-gba-verification.json'))
    args = parser.parse_args()
    build = args.build_dir
    _, symbols, _ = elf_info(build/'dok-diagnostic.elf')
    metadata = json.loads((build/'build-report.json').read_text(encoding='utf-8'))
    frontend = Frontend(args.core.resolve(), build)
    frontend.load(build/'dok-diagnostic.gba')

    def diag():
        return dict(zip(NAMES, struct.unpack('<12I', frontend.read(symbols['g_time_diag'], 48))))

    excluded_counter = 0
    def check(d):
        assert d['error'] == 0 and d['magic'] == 0x54494d36, d
        active_counter=d['counter']-excluded_counter
        assert d['ticks'] == active_counter*1193182//31195136, d
        assert d['remainder'] == active_counter*1193182 % 31195136, d
        assert d['step_now'] == d['steps']*100 and d['debt'] == d['ticks']-d['step_now'], d
        assert d['callbacks'] == d['steps'] and d['last_callback'] == d['step_now'], d
        assert frontend.diag(symbols['g_diag'])['fault'] == 0

    def ready():
        nonlocal excluded_counter
        for _ in range(1200):
            frontend.run(1)
            d = diag()
            active=start_gameplay(frontend,symbols) if d['magic']==0x54494d36 else False
            if active and d['magic'] == 0x54494d36 and d['polls'] >= 3:
                excluded_numerator=d['counter']*1193182-d['ticks']*31195136-d['remainder']
                assert excluded_numerator%1193182==0
                excluded_counter=excluded_numerator//1193182
                check(d)
                return d
        raise AssertionError(('time probe did not start', diag()))

    start = ready()
    frontend.run(600)
    end = diag()
    check(end)
    # A sample in the slow renderer may precede the end of the emulator frame
    # by one entire render. Compare deltas with a generous one-render margin.
    slow = 'g_render_diag' in symbols
    expected = 600*280896/64
    assert abs((end['counter']-start['counter'])-expected) < (200000 if slow else 12000), (start, end)
    assert end['counter'] >> 16 > start['counter'] >> 16, 'cascaded timer never rolled low word'
    frontend.screenshot(build/'time.png')
    frontend.core.retro_reset()
    ct.memset(frontend.pointer(0x02000000, metadata['ewram_static_bytes']), 0xa5, metadata['ewram_static_bytes'])
    restart = ready()
    frontend.run(120)
    check(diag())
    report = dict(task='G06', integrated_renderer=slow, clock_runtime_passed=True,
                  edge_vectors_on_arm='passed' if 'dok_time_cases' in symbols else 'separate diagnostic ROM',
                  core_version=frontend.version, core_sha256=sha(args.core),
                  rom_sha256=sha(build/'dok-diagnostic.gba'), rom_bytes=(build/'dok-diagnostic.gba').stat().st_size,
                  ewram_static_bytes=metadata['ewram_static_bytes'],
                  start=start, after_600_vblanks=end, restart_after_poison=restart,
                  exact_rational_samples=True, hardware_low_word_rollover=True,
                  title_pause_excluded_counter=excluded_counter,
                  physical_gba_tested=False, original_bios_tested=False)
    args.report.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    frontend.core.retro_unload_game()
    frontend.core.retro_deinit()
    print(f"G06 {'renderer' if slow else 'standalone'}: ARM vectors, rational clock, fixed steps, callbacks, poisoned reset passed")


if __name__ == '__main__':
    main()
