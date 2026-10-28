"""Execute the bilingual G03 package and state-preserving language switch in mGBA."""

import sys as _layout_sys
from pathlib import Path as _LayoutPath
_layout_root = _LayoutPath(__file__).resolve().parents[3]
for _layout_dir in ("scripts", "tools/originals", "tools/reports", "ports/gba/scripts"):
    _layout_sys.path.append(str(_layout_root / _layout_dir))

import argparse
import ctypes as ct
import json
import os
from pathlib import Path
import struct

from verify_gba import Frontend, sha

ASSET_NAMES = ('magic', 'status', 'language', 'level', 'views_checked', 'resources', 'package_bytes',
               'text_hash', 'state_hash', 'switches', 'text_size', 'last_keys',
               'player_x', 'player_y', 'player_z', 'startup_switch_preserved')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--core', type=Path, default=Path(os.environ['LOCALAPPDATA']) / 'Programs/mGBA/libretro-20261024/mgba_libretro.dll')
    parser.add_argument('--build-dir', type=Path, default=Path('build/gba/gba-assets'))
    parser.add_argument('--assets-bin', type=Path, default=Path('build/gba/assets/assets.bin'))
    args = parser.parse_args()
    build = args.build_dir
    metadata = json.loads((build/'build-report.json').read_text())
    rom = build/'dok-diagnostic.gba'
    assert sha(rom) == metadata['rom_sha256']
    frontend = Frontend(args.core.resolve(), build)
    frontend.load(rom)
    symbols = metadata['symbols']
    address = symbols['g_assets_diag']
    base = symbols['g_diag']

    def asset_diag():
        return dict(zip(ASSET_NAMES, struct.unpack('<12IiiiI', frontend.read(address, 64))))

    def ready():
        for _ in range(100):
            frontend.run(10)
            diag = frontend.diag(base)
            assert diag['fault'] == 0, ('ARM panic', diag, asset_diag())
            if diag['frames'] >= 2:
                break
        else:
            frontend.screenshot(build/'startup-failed.png')
            raise AssertionError(('ARM package initialization timed out', diag, asset_diag()))
        asset = asset_diag()
        assert asset['magic'] == 0x41535433 and asset['status'] == 0
        assert asset['views_checked'] == 6 and asset['startup_switch_preserved'] == 1, asset
        assert (asset['player_x'], asset['player_y'], asset['player_z']) == (15120, -300, 22800), asset
        assert asset['package_bytes'] == args.assets_bin.stat().st_size
        return asset

    initial = ready()
    assert initial['language'] == 0
    assert symbols['__iwram_reserved_bytes'] == 8192
    embedded = frontend.read(symbols['dok_gba_assets'], initial['package_bytes'])
    assert embedded == args.assets_bin.read_bytes(), 'Embedded ROM package differs from offline output'
    world_address = symbols['g_assets_world']
    world_size = 33864  # G02 ARM ABI; also asserted by the reader's wire layout checks.
    initial_world = frontend.read(world_address, world_size)
    frontend.run(8)
    frontend.screenshot(build/'assets-ru.png')

    def switch(expected_language):
        previous_switches = asset_diag()['switches']
        for _ in range(50):
            frontend.run(2, [2])  # Libretro SELECT, GBA KEYINPUT bit2.
            if asset_diag()['switches'] != previous_switches:
                break
        else:
            raise AssertionError(('Language switch timed out', asset_diag()))
        # The reader validates the selected view before returning. Keep SELECT
        # released until the ARM loop has actually sampled its release.
        for _ in range(150):
            frontend.run(2)
            if asset_diag()['last_keys'] == 0 and frontend.diag(base)['keys'] == 0:
                break
        else:
            raise AssertionError(('SELECT release not observed', asset_diag()))
        frontend.run(8)
        current = asset_diag()
        assert current['status'] == 0 and current['language'] == expected_language, current
        assert current['switches'] == previous_switches + 1
        assert frontend.read(world_address, world_size) == initial_world, 'Language switch changed mutable world'
        return current

    english = switch(1)
    assert english['text_hash'] != initial['text_hash'], 'Language switch must change the actual ROM text pool'
    frontend.screenshot(build/'assets-en.png')
    russian = switch(0)
    assert russian['text_hash'] == initial['text_hash']
    frontend.screenshot(build/'assets-ru-return.png')
    frontend.core.retro_reset()
    # Verify cold initialization even if the emulator's cleared memory is not relied upon.
    ct.memset(frontend.pointer(0x02000000, metadata['ewram_static_bytes']), 0xa5, metadata['ewram_static_bytes'])
    restarted = ready()
    assert restarted['language'] == 0 and restarted['switches'] == 0
    result = {'task': 'G03', 'status': 'passed', 'emulator': frontend.name,
              'emulator_version': frontend.version, 'core_sha256': sha(args.core),
              'rom_sha256': sha(rom), 'rom_bytes': rom.stat().st_size,
              'package_sha256': sha(args.assets_bin), 'embedded_package_byte_identical': True,
              'ewram_static_bytes': metadata['ewram_static_bytes'], 'iwram_stack_bios_reserve': 8192,
              'initial': initial, 'english': english, 'russian_return': russian, 'restart_after_poison': restarted,
              'all_six_views_reset_on_arm': True, 'mutable_world_preserved_byte_for_byte': True,
              'physical_gba_tested': False, 'gameplay_or_30fps_verified': False,
              'screenshots': {p.name: sha(p) for p in (build/'assets-ru.png', build/'assets-en.png', build/'assets-ru-return.png')}}
    report = Path('analysis/gba/g03-gba-verification.json')
    report.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    (build/'run-log.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    frontend.core.retro_unload_game()
    frontend.core.retro_deinit()
    print(f"mGBA: six original-derived views reset on ARM; RU/EN text pools switch and preserve all {world_size} runtime bytes")
    print(f"ROM {result['rom_bytes']} bytes, EWRAM {result['ewram_static_bytes']} bytes; report {report}")


if __name__ == '__main__':
    main()
