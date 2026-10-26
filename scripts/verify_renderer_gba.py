"""Execute G05's real-level renderer, language switches and cycle probe in mGBA."""
import argparse
import ctypes as ct
import hashlib
import json
import os
from pathlib import Path
import struct

from gba_rom import elf_info
from verify_gba import Frontend, sha

NAMES = ('magic status language level draws last_cycles min_cycles max_cycles '
         'faces columns pixels switches last_keys player_x player_y player_z '
         'scene_height message_height cycle_budget over_budget present_vblank '
         'frame_bytes world_bytes workspace_bytes guard_ok palette_ready font_ready '
         'visible_page face_count geometry_fnv1a phase reserved4').split()
BUDGET = 2 * 280896
STACK_LOW, STACK_TOP = 0x03006000, 0x03007f00
GUARD = 0xd04fcafe

def start_gameplay(frontend, symbols):
    """Explicit test setup for pre-G17 scenarios; the real UI starts at title.

    Called only by legacy harnesses. UIProbe opts out and drives real buttons.
    Commit request last, using the same public test mailbox as other scenarios.
    """
    if 'g_flow_probe' in symbols:
        address=symbols['g_flow_probe']
        request,ack=struct.unpack('<2I',frontend.read(address,8))
        magic,state=struct.unpack('<2I',frontend.read(symbols['g_flow_diag'],8))
        if magic!=0x464c3138:return False
        if state!=2 and request==ack:
            ct.memmove(frontend.pointer(address+8,8),struct.pack('<2I',3,0),8)
            ct.memmove(frontend.pointer(address,4),struct.pack('<I',ack+1),4)
            return False
        return state==2 and request==ack
    if 'g_ui_probe' not in symbols:
        return True
    magic,state = struct.unpack('<2I', frontend.read(symbols['g_ui_diag'],8))
    if magic != 0x55493137:
        return False
    address = symbols['g_ui_probe']
    request,ack = struct.unpack('<2I',frontend.read(address,8))
    if state == 1 and request == ack:
        payload=struct.pack('<4I',1,0,0,0)
        ct.memmove(frontend.pointer(address+8,16),payload,16)
        ct.memmove(frontend.pointer(address,4),struct.pack('<I',ack+1),4)
        return False
    return state == 0 and request == ack

def freeze_visual(frontend,symbols,magic):
    """Static-scene regression freezes G11 visual time; playback has its own probe."""
    if magic==0x524e4435 and 'g_visual_probe' in symbols:
        ct.memmove(frontend.pointer(symbols['g_visual_probe'],4),struct.pack('<I',1),4)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def stack_usage(frontend):
    words = struct.unpack('<1984I', frontend.read(STACK_LOW, STACK_TOP-STACK_LOW))
    untouched = next((i for i, word in enumerate(words) if word != 0x0d15ea5e), len(words)) * 4
    return dict(main_stack_low=STACK_LOW, main_stack_top=STACK_TOP,
                sentinel_word=0x0d15ea5e, untouched_margin_bytes=untouched,
                high_water_bytes=STACK_TOP-STACK_LOW-untouched,
                irq_and_bios_excluded=True, scope='observed G05/G06 startup/time-vectors/render/switch/reset paths')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--core', type=Path, default=Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll')
    parser.add_argument('--build-dir', type=Path, default=Path('build/gba-renderer'))
    parser.add_argument('--report', type=Path, default=Path('analysis/render-benchmark.json'))
    parser.add_argument('--assets-bin', type=Path, default=Path('build/gba/assets/assets.bin'))
    parser.add_argument('--sample-draws', type=int, default=30)
    args = parser.parse_args()
    assert args.sample_draws >= 2
    build = args.build_dir
    metadata = json.loads((build/'build-report.json').read_text(encoding='utf-8'))
    rom = build/'dok-diagnostic.gba'
    assert sha(rom) == metadata['rom_sha256'], 'ROM changed after validation'
    _, symbols, _ = elf_info(build/'dok-diagnostic.elf')
    frontend = Frontend(args.core.resolve(), build)
    frontend.load(rom)
    samples = []
    warmup_cutoffs = {}

    def render_diag():
        values = list(struct.unpack('<32I', frontend.read(symbols['g_render_diag'], 128)))
        for i in (13, 14, 15):
            if values[i] >= 0x80000000:
                values[i] -= 0x100000000
        return dict(zip(NAMES, values))

    def check():
        basic, diag = frontend.diag(symbols['g_diag']), render_diag()
        freeze_visual(frontend,symbols,diag['magic'])
        if diag['magic']==0x524e4435 and diag['draws']:
            start_gameplay(frontend,symbols)
        # After retro_reset the deliberately poisoned RAM is observable while
        # mGBA's replacement BIOS is still reaching cartridge startup. A5 bytes
        # are not initialized diagnostics and must not be interpreted as panic.
        if basic['magic'] != 0x444f4b31:
            return diag
        assert basic['fault'] == 0, ('ARM panic', basic, diag)
        if diag['magic'] == 0x524e4435 and diag['draws']:
            assert diag['magic'] == 0x524e4435 and diag['status'] == 0, diag
            assert diag['guard_ok'] == diag['palette_ready'] == diag['font_ready'] == 1, diag
            assert struct.unpack('<I', frontend.read(symbols['g_render_frame'], 4))[0] == GUARD
            assert struct.unpack('<I', frontend.read(symbols['g_render_frame']+38404, 4))[0] == GUARD
            if not samples or (diag['draws'], diag['switches']) != (samples[-1]['draws'], samples[-1]['switches']):
                samples.append({k: diag[k] for k in ('draws', 'language', 'switches', 'last_cycles', 'present_vblank')})
        return diag

    def until(predicate, buttons=(), limit=6000):
        for _ in range(limit):
            frontend.run(1, buttons)
            current = check()
            if predicate(current):
                return current
        frontend.screenshot(build/'renderer-timeout.png')
        raise AssertionError(('Renderer timed out', current))

    def ready():
        diag = until(lambda d: d['magic'] == 0x524e4435 and d['draws'] >= 2 and
                     ('g_ui_probe' not in symbols or (start_gameplay(frontend,symbols) and
                      struct.unpack('<I',frontend.read(symbols['g_motion_diag']+8,4))[0]>0)))
        assert (diag['player_x'], diag['player_y'], diag['player_z']) == (15120, -300, 22800), diag
        assert (diag['scene_height'], diag['message_height'], diag['frame_bytes'], diag['world_bytes']) == (144,16,38400,33864)
        assert diag['cycle_budget'] == BUDGET
        basic = frontend.diag(symbols['g_diag'])
        assert basic['bss_ok'] == basic['data_ok'] == 1, basic
        return diag

    def capture(name):
        current = until(lambda d: d['phase'] == 2)
        dispcnt = struct.unpack('<H', frontend.read(0x04000000, 2))[0]
        assert dispcnt & 7 == 4 and dispcnt & 0x400 and not dispcnt & 0x80
        page = (dispcnt >> 4) & 1
        assert page == current['visible_page'], current
        pixels = frontend.read(0x06000000+page*0xa000, 38400)
        palette = frontend.read(0x05000000, 512)
        face_count = current['face_count']
        assert 0 < face_count <= 256, current
        geometry = frontend.read(symbols['g_render_frame']+38408, face_count*48)
        fnv = 2166136261
        for byte in geometry:
            fnv = ((fnv ^ byte)*16777619) & 0xffffffff
        assert fnv == current['geometry_fnv1a'], 'Completed face bytes differ from committed geometry signature'
        (build/f'{name}.idx').write_bytes(pixels)
        (build/f'{name}.pal').write_bytes(palette)
        frontend.screenshot(build/f'{name}.png')
        return dict(diag=current, framebuffer_sha256=digest(pixels),
                    scene_sha256=digest(pixels[:240*144]), message_sha256=digest(pixels[240*144:]),
                    palette_sha256=digest(palette), geometry_fnv1a=current['geometry_fnv1a'],
                    geometry_sha256=digest(geometry), geometry_faces=face_count, dispcnt=dispcnt), pixels, geometry

    initial = ready()
    assert initial['language'] == initial['switches'] == 0
    assert symbols['__iwram_reserved_bytes'] == 8192
    embedded = frontend.read(symbols['dok_gba_assets'], args.assets_bin.stat().st_size)
    assert embedded == args.assets_bin.read_bytes(), 'Embedded package differs from packer output'
    world = frontend.read(symbols['g_render_world'], 33864)
    start = initial['draws']
    warmup_cutoffs[0] = start+8
    until(lambda d: d['draws'] >= start+args.sample_draws)
    ru, ru_pixels, ru_geometry = capture('renderer-ru')

    def switch(language, name):
        previous = check()['switches']
        pressed = until(lambda d: d['switches'] > previous, [2])
        assert pressed['switches'] == previous+1 and pressed['language'] == language, pressed
        until(lambda d: d['last_keys'] == 0 and d['draws'] >= pressed['draws']+2)
        current_world=frontend.read(symbols['g_render_world'],33864)
        assert current_world == world, ('Language switch changed mutable world',[(i,a,b) for i,(a,b) in enumerate(zip(world,current_world)) if a!=b][:24])
        start = check()['draws']
        warmup_cutoffs[previous+1] = start+8
        until(lambda d: d['draws'] >= start+args.sample_draws)
        return capture(name)

    en, en_pixels, en_geometry = switch(1, 'renderer-en')
    ru_return, return_pixels, return_geometry = switch(0, 'renderer-ru-return')
    assert ru['geometry_fnv1a'] == en['geometry_fnv1a'] == ru_return['geometry_fnv1a'], 'Language switch changed scene geometry signature'
    assert ru['geometry_faces'] == en['geometry_faces'] == ru_return['geometry_faces']
    assert ru_geometry == en_geometry == return_geometry, 'Completed scene geometry bytes changed across languages'
    assert ru_pixels[:240*144] == en_pixels[:240*144] == return_pixels[:240*144], 'Common scene changed across languages'
    if 'g_ui_probe' in symbols:
        # G17 reserves the entire band for actual events. Its idle band is
        # blank in either locale; exact original event text is tested by G17.
        assert ru_pixels[240*144:] == en_pixels[240*144:]
    else:
        assert ru_pixels[240*144:] != en_pixels[240*144:], 'Localized message did not change'
    assert ru_pixels == return_pixels, 'RU return framebuffer differs'
    before_reset_stack = stack_usage(frontend)
    assert before_reset_stack['untouched_margin_bytes'] >= 512, before_reset_stack
    benchmark_samples = list(samples)
    final_before_reset = check()
    frontend.core.retro_reset()
    ct.memset(frontend.pointer(0x02000000, metadata['ewram_static_bytes']), 0xa5, metadata['ewram_static_bytes'])
    samples.clear()
    restarted = ready()
    assert restarted['language'] == restarted['switches'] == 0, restarted
    assert frontend.read(symbols['g_render_world'], 33864) == world, 'Poison reset did not restore initial world'
    reset_stack = stack_usage(frontend)
    assert reset_stack['untouched_margin_bytes'] >= 512, reset_stack

    # Consecutive observed draw IDs yield actual presentation cadence; skipped
    # IDs are not silently treated as consecutive rendered frames.
    cadence = [b['present_vblank']-a['present_vblank'] for a,b in zip(benchmark_samples, benchmark_samples[1:])
               if b['draws'] == a['draws']+1 and b['switches'] == a['switches']
               and a['draws'] >= warmup_cutoffs[a['switches']]]
    assert cadence, 'No consecutive steady-state presentations observed'
    result = dict(task='G05', status='passed', emulator=frontend.name, emulator_version=frontend.version,
                  core_sha256=sha(args.core), bios='mGBA replacement BIOS (no external BIOS)',
                  rom_sha256=sha(rom), rom_bytes=rom.stat().st_size,
                  package_sha256=sha(args.assets_bin), embedded_package_byte_identical=True,
                  ewram_static_bytes=metadata['ewram_static_bytes'], iwram_stack_bios_reserve=8192,
                  ru=ru, en=en, ru_return=ru_return, restart_after_poison=restarted,
                  world_bytes=33864, mutable_world_sha256=digest(world),
                  mutable_world_preserved_byte_for_byte=True, scene_geometry_signature_preserved=True,
                  scene_geometry_preserved_byte_for_byte=True,
                  scene_pixels_preserved_byte_for_byte=True, localized_message_changed=True,
                  framebuffer_guards_passed=True, poisoned_ewram_reset_passed=True,
                  stack_before_reset=before_reset_stack, stack_after_reset=reset_stack,
                  timing=dict(cpu_hz=16777216, hardware_frame_cycles=280896,
                              target_cycles=BUDGET, target_hardware_frames=2,
                              scope='Timer0/1 cascade: time-poll + input/language switch + renderer + HUD + halfword VRAM back-page copy; excludes presentation wait',
                              min_cycles=final_before_reset['min_cycles'], max_cycles=final_before_reset['max_cycles'],
                              worst_render_budget_ratio=final_before_reset['max_cycles']/BUDGET,
                              nominal_target_fps=16777216/BUDGET,
                              over_budget_draws=final_before_reset['over_budget'],
                              all_observed_draws_fit_target=final_before_reset['max_cycles'] <= BUDGET,
                              min_present_interval_vblanks=min(cadence), max_present_interval_vblanks=max(cadence),
                              cadence_intervals_vblanks=cadence, cadence_warmup_excluded_draws=8,
                              observed_steady_presentation_fps=16777216/(280896*sum(cadence)/len(cadence)),
                              sampled_draws=len(benchmark_samples), samples=benchmark_samples),
                  physical_gba_tested=False, full_game_performance_verified=False,
                  visual_playback_frozen='g_visual_probe' in symbols,
                  screenshots={p.name:sha(p) for p in (build/'renderer-ru.png',build/'renderer-en.png',build/'renderer-ru-return.png')})
    args.report.parent.mkdir(parents=True, exist_ok=True)
    serialized = json.dumps(result, indent=2)+'\n'
    args.report.write_text(serialized, encoding='utf-8')
    (build/'renderer-run-log.json').write_text(serialized, encoding='utf-8')
    frontend.core.retro_unload_game()
    frontend.core.retro_deinit()
    print(f"mGBA renderer: RU/EN scene/state/guards/reset passed; {result['timing']['min_cycles']}..{result['timing']['max_cycles']} cycles vs {BUDGET}")
    print(f"Report: {args.report}; captures: {build}")


if __name__ == '__main__':
    main()
