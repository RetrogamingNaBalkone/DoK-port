"""G11: exercise visual playback and sprite scenes in the real ARM ROM.

Expected animation metadata is decoded from the original RES independently of
the C player. This does not assert whole-session DOS raster equivalence.
"""
import argparse
import ctypes as ct
import hashlib
import json
import os
from pathlib import Path
import struct

from analyze_resources import parse
from gba_rom import elf_info
from probe_render_layout import frames
from verify_gba import Frontend, sha
from verify_renderer_gba import NAMES, stack_usage, start_gameplay


def load_json(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def original_animations(language):
    path = Path('originals/pkr') / ('RUS/PKR.RES' if language == 0 else 'ENG/PKE.RES')
    data = path.read_bytes()
    resources = {entry['name_hex']: data[entry['offset']:entry['offset'] + entry['size']]
                 for entry in reversed(parse(data))}
    return resources, dict(path=path.as_posix(), sha256=sha(path))


class Probe:
    preserve_title = False
    def __init__(self, core, build):
        self.build = build
        self.metadata = load_json(build / 'build-report.json')
        self.rom = build / 'dok-diagnostic.gba'
        assert sha(self.rom) == self.metadata['rom_sha256']
        _, self.symbols, _ = elf_info(build / 'dok-diagnostic.elf')
        self.frontend = Frontend(core.resolve(), build)
        self.frontend.load(self.rom)

    def write(self, address, payload):
        ct.memmove(self.frontend.pointer(address, len(payload)), payload, len(payload))

    def words(self, symbol, count, offset=0):
        return struct.unpack('<' + 'I' * count,
                             self.frontend.read(self.symbols[symbol] + offset, count * 4))

    def diag(self):
        return dict(zip(NAMES, self.words('g_render_diag', 32)))

    def until(self, predicate, buttons=(), limit=6000):
        for _ in range(limit):
            self.frontend.run(1, buttons)
            basic = self.frontend.diag(self.symbols['g_diag'])
            if basic['magic'] == 0x444f4b31:
                assert basic['fault'] == 0, basic
            diag = self.diag()
            if not self.preserve_title and diag['magic']==0x524e4435 and diag['draws']:
                start_gameplay(self.frontend,self.symbols)
            if diag['magic'] == 0x524e4435 and diag['draws']:
                assert diag['status'] == 0 and diag['guard_ok'] == 1, diag
                assert self.words('g_render_frame', 1)[0] == 0xd04fcafe
                assert self.words('g_render_frame', 1, 38404)[0] == 0xd04fcafe
            if predicate(diag):
                return diag
        raise AssertionError(('G11 timeout', self.diag()))

    def present(self):
        previous = self.diag()['draws']
        return self.until(lambda d: d['draws'] > previous and d['phase'] == 2)

    def object_words(self, index):
        # Compact world LE layout asserted in runtime/asset tests: pointer4,
        # Object36 with flags/frame/budget/interval/A/B/known/reserved first.
        return struct.unpack('<8H', self.frontend.read(
            self.symbols['g_render_world'] + 4 + index * 36, 16))

    def language(self, language):
        if self.diag()['language'] != language:
            previous = self.diag()['switches']
            self.until(lambda d: d['switches'] > previous, buttons=(2,))
            self.until(lambda d: d['language'] == language and d['last_keys'] == 0
                       and d['phase'] == 2)

    def capture(self, name):
        diag = self.present()
        raw = self.frontend.read(0x06000000 + diag['visible_page'] * 0xa000, 38400)
        palette = self.frontend.read(0x05000000, 512)
        destination = self.build / 'sprite-check' / name
        destination.parent.mkdir(exist_ok=True)
        destination.with_suffix('.idx').write_bytes(raw)
        destination.with_suffix('.pal').write_bytes(palette)
        self.frontend.screenshot(destination.with_suffix('.png'))
        return dict(path=destination.with_suffix('.png').as_posix(),
                    framebuffer_sha256=hashlib.sha256(raw).hexdigest(),
                    cycles=diag['last_cycles'], faces=diag['face_count'])

    def close(self):
        self.frontend.core.retro_unload_game()
        self.frontend.core.retro_deinit()

    def visual(self):
        names = 'magic error steps completed now object frame active deadline resource budget'.split()
        return dict(zip(names, self.words('g_visual_diag', 11)))

    def poll(self, tick, index, slot=0, loop=False):
        request = self.words('g_visual_probe', 3)[1] + 1
        previous = self.diag()['draws']
        address = self.symbols['g_visual_probe']
        self.write(address + 12, struct.pack('<4I', tick & 0xffffffff, index, slot, loop))
        self.write(address + 4, struct.pack('<I', request))
        self.until(lambda d: self.words('g_visual_probe', 3)[2] == request
                   and d['draws'] > previous and d['phase'] == 2)
        state = self.visual()
        assert state['magic'] == 0x56495331 and state['error'] == 0, state
        assert state['now'] == tick & 0xffffffff, state
        return state

    def camera(self, level, x, y, z, heading=0):
        request = self.words('g_camera_probe', 2)[0] + 1
        address = self.symbols['g_camera_probe']
        self.write(address + 8, struct.pack('<I4i', level, x, y, z, heading))
        self.write(address, struct.pack('<I', request))
        self.until(lambda d: self.words('g_camera_probe', 2)[1] == request and d['phase'] == 2)


def playback(probe, language, index, slot, name, raw, loop=False):
    sequence = [metadata for metadata, _ in frames(raw)]
    # Keep negative intervals away from uint32 rollover so their unsigned
    # wrapped deadline is already due, as specified by the original scheduler.
    tick = 1000000
    probe.poll(tick, index)
    state = probe.poll(tick, index, slot, loop)
    assert state['frame'] == 0 and state['active'] == 1, state
    steps = []
    completed_before = state['completed']
    iterations = len(sequence) + (2 if loop else 0)
    for transition in range(iterations):
        frame_index = transition % len(sequence)
        metadata = sequence[frame_index]
        words = probe.object_words(index)
        assert words[1] == frame_index and words[3] == metadata['interval_bits'], (state, metadata, words)
        assert words[4:6] == (metadata['parameter_a'] & 65535, metadata['parameter_b'] & 65535)
        expected_deadline = (tick + metadata['interval_ticks']) & 0xffffffff
        assert state['deadline'] == expected_deadline, (state, metadata, tick)
        if metadata['interval_ticks'] > 0:
            early = probe.poll(expected_deadline - 1, index)
            assert early['frame'] == frame_index and early['active'] == 1, early
            tick = expected_deadline
        state = probe.poll(tick, index)
        # Native dispatch intentionally shortens its owner visit bound when
        # another owner disappears. A due event may wait for the next sweep.
        # Keep the same absolute time; never advance or hide a missed deadline.
        for _ in range(256):
            if state['frame'] != frame_index or not state['active'] or state['deadline'] != expected_deadline:
                break
            state = probe.poll(tick, index)
        else:
            raise AssertionError(('Due animation never dispatched', state))
        end = not loop and transition == len(sequence) - 1
        if end:
            assert state['active'] == 0 and state['completed'] == completed_before + 1, state
            assert state['frame'] == len(sequence) - 1, state
        else:
            assert state['frame'] == (frame_index + 1) % len(sequence) and state['active'] == 1, state
        steps.append(dict(frame=frame_index, interval=metadata['interval_ticks'],
                          deadline=expected_deadline, poll_tick=tick, after=state))
    return dict(language=language, object=index, slot=slot, resource=name,
                loop=loop, transitions=steps, negative_intervals=sum(
                    frame['interval_ticks'] < 0 for frame in sequence))


def projectile_scenes(probe, language, model, resources):
    """Position original projectile images; compare ARM faces with scalar math."""
    captures = []
    probe.camera(0, 15120, -300, 22800, 90)
    probe.write(probe.symbols['g_sprite_probe'], bytes(20))
    diag = probe.present()
    background = probe.frontend.read(0x06000000 + diag['visible_page'] * 0xa000, 38400)
    for index, obj in enumerate(model['objects']):
        if obj['class'] != 8:
            continue
        sequence = frames(resources[obj['animation']])
        for frame_index in sorted({0, len(sequence) - 1}):
            im = sequence[frame_index][0]
            probe.write(probe.symbols['g_render_world'] + 4 + index * 36 + 2,
                        struct.pack('<H', frame_index))
            width = im['width'] * obj['width_parameter']
            height = im['height'] * obj['width_parameter']
            for depth in (320, 640):
                probe.write(probe.symbols['g_sprite_probe'], struct.pack(
                    '<II3i', 1, index, 15120 - depth, -250, 22800))
                capture = probe.capture(f'projectile-{language}-{index}-{frame_index}-{depth}')
                diag = probe.diag()
                faces = probe.frontend.read(probe.symbols['g_render_frame'] + 38408,
                                            diag['face_count'] * 48)
                found = [struct.unpack_from('<11iHh', faces, offset)
                         for offset in range(0, len(faces), 48)
                         if struct.unpack_from('<H', faces, offset + 44)[0] == index]
                assert len(found) == 1, (index, frame_index, depth, found)
                factor = 6553600 // (depth + 200)
                left = (159 + ((-(width // 2) * factor) >> 15)) * 3 // 4
                right = (159 + (((width // 2) * factor) >> 15)) * 3 // 4
                top = (79 + (((50 - height) * factor) >> 15)) * 9 // 10
                bottom = (79 + ((50 * factor) >> 15)) * 9 // 10
                expected = (left, right, top, top, bottom, bottom, depth, depth,
                            0, im['width'] - 1, max(0, depth - width // 4), index, 0)
                assert found[0] == expected, (found[0], expected)
                raw = probe.frontend.read(0x06000000 + diag['visible_page'] * 0xa000, 38400)
                changed = sum(a != b for a, b in zip(raw[:144 * 240], background[:144 * 240]))
                assert changed > 0, ('Projectile invisible', index, frame_index, depth)
                captures.append(dict(language=language, object=index, frame=frame_index,
                                     depth=depth, changed_scene_pixels=changed,
                                     geometry=list(expected), **capture))
    probe.write(probe.symbols['g_sprite_probe'], bytes(20))
    probe.present()
    return captures


def water_tail(probe, indices):
    """Original water callback publishes old counter, then increments it."""
    index = next(i for i in indices if probe.object_words(i)[6] & 8)
    states = []
    tick = probe.visual()['now']
    while True:
        before = probe.object_words(index)
        state = probe.poll(tick, index)
        # A neighbour's removal may postpone this owner until the next sweep.
        for _ in range(256):
            after = probe.object_words(index)
            if after[1] != before[1] or not state['active']:
                break
            state = probe.poll(tick, index)
        else:
            raise AssertionError(('Water event did not dispatch', state))
        assert after[6] & 8 and not after[6] & 1, after
        if before[1] < 18:
            assert after[4] == before[1] and after[1] == before[1] + 1, (before, after)
        else:
            assert after[1] == 18 and after[4] == 17 and state['active'] == 0, after
        states.append(dict(counter=after[1], displayed_crop=after[4], active=state['active']))
        if not state['active']:
            break
        assert len(states) <= 19, states
    return dict(object=index, states=states, crop_then_increment_verified=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=Path('build/g11'))
    parser.add_argument('--core', type=Path, default=Path(os.environ['LOCALAPPDATA']) /
                        'Programs/mGBA/libretro-20261024/mgba_libretro.dll')
    parser.add_argument('--report', type=Path, default=Path('analysis/g11-sprites-gba.json'))
    parser.add_argument('--assets-bin', type=Path, default=Path('build/gba/assets/assets.bin'))
    args = parser.parse_args()
    probe = Probe(args.core, args.build_dir)
    results, captures, sources = [], [], []
    try:
        probe.until(lambda d: d['draws'] >= 3 and d['phase'] == 2)
        package = args.assets_bin.read_bytes()
        assert probe.frontend.read(probe.symbols['dok_gba_assets'], len(package)) == package, \
            'Embedded resource package differs from packer output'
        probe.write(probe.symbols['g_camera_capture_hold'], struct.pack('<I', 1))
        # Observe ordinary runtime progression in LEVEL2's water scene before
        # freezing controlled time. LEVEL1 has no visible water at startup.
        model = load_json('build/runtime/ru/LEVEL2-runtime.json')
        cell = next(i for i, glyph in enumerate(bytes.fromhex(model['maps'][2]))
                    if glyph == ord('<'))
        probe.camera(1, cell % 64 * 480 + 240, -300, cell // 64 * 480 + 240)
        changed = set()
        before = [probe.object_words(i)[1] for i in range(256)]
        for _ in range(24):
            probe.present()
            changed.update(i for i in range(256) if probe.object_words(i)[1] != before[i])
            if changed:
                break
        water = [i for i, obj in enumerate(model['objects']) if obj['glyph'] in
                 (ord('<'), ord('>'), ord('{'), ord('}'))]
        trace = [[probe.object_words(i)[1] for i in water]]
        for _ in range(4):
            probe.present()
            trace.append([probe.object_words(i)[1] for i in water])
        advances = [(b - a) % 19 for before_row, after_row in zip(trace, trace[1:])
                    for a, b in zip(before_row, after_row)]
        assert any(1 < advance < 18 for advance in advances), \
            ('Automatic animation remains limited to one step per draw', trace)
        automatic = dict(level=2, changed_objects=sorted(changed), water_objects=water,
                         phase_trace_per_presentation=trace, multiple_steps_per_draw_verified=True)
        assert changed, 'No automatic animation progressed during ordinary ROM execution'
        probe.write(probe.symbols['g_motion_probe'], struct.pack('<I', 1))
        probe.write(probe.symbols['g_visual_probe'], struct.pack('<I', 1))
        water_check = water_tail(probe, automatic['water_objects'])
        probe.camera(0, 15120, -300, 22800, 90)
        model = load_json('build/runtime/ru/LEVEL1-runtime.json')
        for language in (0, 1):
            probe.language(language)
            resources, source = original_animations(language)
            sources.append(source)
            candidates = []
            seen = set()
            for index, obj in enumerate(model['objects']):
                bindings = [(0xfff1, obj['animation'])] + [
                    (field['offset'], field['hex']) for field in obj['fields'] if field['kind'] == 4]
                for slot, name in bindings:
                    if not name or name in seen:
                        continue
                    seen.add(name)
                    metadata = [frame for frame, _ in frames(resources[name])]
                    negative = any(frame['interval_ticks'] < 0 for frame in metadata)
                    if negative or not candidates:
                        candidates.append((index, slot, name, negative))
            assert sum(negative for _, _, _, negative in candidates) >= 2, candidates
            for index, slot, name, negative in candidates:
                results.append(playback(probe, language, index, slot, name, resources[name]))
                if not negative:
                    results.append(playback(probe, language, index, slot, name, resources[name], loop=True))
            captures.append(dict(language=language, **probe.capture(f'playback-{language}')))
            captures.extend(projectile_scenes(probe, language, model, resources))
        stack = stack_usage(probe.frontend)
        assert stack['untouched_margin_bytes'] >= 512, stack
        assert not probe.frontend.callback_errors, probe.frontend.callback_errors
        for source in sources:
            assert sha(source['path']) == source['sha256'], source
        report = dict(task='G11', status='passed', rom_sha256=sha(probe.rom),
                      script_sha256=sha(__file__), core_sha256=sha(args.core),
                      emulator=probe.frontend.name, emulator_version=probe.frontend.version,
                      automatic=automatic, water_callback=water_check,
                      playback=results, captures=captures, stack=stack,
                      embedded_package_byte_identical=True, package_sha256=sha(args.assets_bin),
                      originals=sources, physical_gba_tested=False, whole_dos_session_equivalence=False)
        args.report.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
        print(f'G11 ARM playback: {len(results)} sequences, automatic animation, guards and stack passed')
    finally:
        probe.close()


if __name__ == '__main__':
    main()
