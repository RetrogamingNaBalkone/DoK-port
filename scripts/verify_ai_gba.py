"""G14 independent ARM perception/navigation scenarios.

Controlled maps isolate original LEVEL parameters and state transitions; these
checks do not claim equivalence to a complete DOS playthrough.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct

from analyze_levels import reference
from verify_combat_gba import CombatProbe, actor_pose, setup_open
from verify_items_gba import MAP_OFFSET
from verify_renderer_gba import stack_usage
from verify_sprites_gba import load_json, original_animations
from verify_gba import sha


def original_monsters(language, level=0):
    resources, source = original_animations(language)
    model = reference(resources[f'LEVEL{level + 1}'.encode().hex()])
    return {obj['glyph']: {offset: value for _, offset, value in obj['fields']}
            for obj in model['objects'] if obj['class'] == 9}, source


def actor_values(probe, model, index):
    obj = model['objects'][index]
    data = probe.frontend.read(probe.symbols['g_render_world'] + 9220 +
                               obj['actor_index'] * 64, 64)
    names = ('known x y z speed step_distance accumulator_a accumulator_b '
             'motion_flags heading angle mode layer direction step ready '
             'command command84 parameter98 bound9e bounda0 glyph previous reserved').split()
    return dict(zip(names, struct.unpack('<I7iI12hBBH', data)))


class AIProbe(CombatProbe):
    def ai_diag(self):
        names = ('magic error steps wakes courses blocked contacts now owners '
                 'object state counter x z heading frame deadline active result').split()
        return dict(zip(names, self.words('g_ai_diag', len(names))))

    def ai(self, op, index=0, tick=10000, count=1, stride=416):
        address = self.symbols['g_ai_probe']
        request = self.words('g_ai_probe', 3)[1] + 1
        previous = self.diag()['draws']
        self.write(address + 12, struct.pack('<5I', op, index, tick & 0xffffffff, count, stride))
        self.write(address + 4, struct.pack('<I', request))
        self.until(lambda d: self.words('g_ai_probe', 3)[2] == request and
                   d['draws'] > previous and d['phase'] == 2)
        result = self.ai_diag()
        assert result['magic'] == 0x41493134 and result['error'] == 0, result
        return result

    def sleeping(self, index):
        self.ai(4, index)
        address = self.symbols['g_render_world'] + 4 + index * 36
        self.write(address + 18, struct.pack('<hh', 0, 0))
        self.write(address + 2, struct.pack('<H', 0))


def reset_level(probe, language, level=0):
    probe.language(language)
    # The release package uses the approved shared balance in both languages.
    probe.camera((level + 1) % 3, 15120, -300, 22800, 90)
    probe.camera(level, 15120, -300, 22800, 90)
    return load_json(f'research/evidence/runtime/{"ru" if language == 0 else "en"}/LEVEL{level + 1}-runtime.json')


def perception(probe, model, language):
    definitions, source = original_monsters(language)
    checks = []
    for glyph in (0x81, 0x8c, 0xa0, 0xa1):
        index = model['glyph_index'][glyph] - 1
        fields = definitions[glyph]
        # Independent explicit policy from docs/gba-balance-proposal.md:
        # boyars use ENG, first witch hybrid, mice retain RUS course period.
        distance = 9000 if glyph == 0x81 else 3400 if glyph == 0xa0 else 4000
        period = 24 if glyph in (0x81, 0x8c) else 4 if glyph == 0xa0 else 8
        for dx, dz, expected in ((distance, 0, False), (distance - 1, 0, True),
                                 (0, distance, False), (0, distance - 1, True),
                                 (distance - 1, distance - 1, True)):
            setup_open(probe, model)
            probe.ai(5)
            probe.sleeping(index)
            actor_pose(probe, model, index, 5000, 5000)
            actor_pose(probe, model, model['player_index'] - 1, 5000 + dx, 5000 + dz)
            # Merely polling is insufficient: native proximity is an image
            # getter side effect and requires consideration of this object.
            hidden = probe.ai(3, index, tick=10000)
            assert hidden['state'] == 0 and not hidden['active'], hidden
            result = probe.ai(1, index)
            assert bool(result['state']) == expected, (glyph, dx, dz, result)
            if expected:
                assert result['state'] == 1 and result['counter'] == period, result
                assert result['active'] == 1 and result['deadline'] == 10416, result
            else:
                assert result['active'] == 0, result
            checks.append(dict(language=language, glyph=glyph, distance=distance,
                               period=period, original_distance=fields[0xba],
                               original_period=fields[0xae], delta=[dx, dz], awake=expected,
                               hidden_poll_did_not_activate=True))
    assert sha(source['path']) == source['sha256']
    return checks


def motion_trace(probe, model, index, blocked=False, repeated=False):
    setup_open(probe, model)
    probe.ai(5)
    probe.sleeping(index)
    actor_pose(probe, model, index, 15120, 15120, 0)
    address = probe.symbols['g_render_world'] + 9220 + model['objects'][index]['actor_index'] * 64
    probe.write(address + 20, bytes(16))
    probe.write(address + 44, bytes(12))
    actor_pose(probe, model, model['player_index'] - 1, 17520, 15120)
    world = probe.symbols['g_render_world']
    duplicate = 10 * 64 + 10
    if repeated:
        probe.write(world + MAP_OFFSET + duplicate, bytes([model['objects'][index]['glyph']]))
    if blocked:
        wall = next(obj['glyph'] for obj in model['objects']
                    if obj['class'] == 4 and not (obj['flags'] & 16))
        probe.write(world + MAP_OFFSET + 4096, bytes([wall]) * 4096)
        probe.write(world + MAP_OFFSET + 4096 + 31 * 64 + 31, b' ')
    start = probe.ai(2, index)
    trace = []
    for _ in range(40):
        state = probe.ai(3, index, tick=start['deadline'])
        actor = actor_values(probe, model, index)
        trace.append({name: state[name] for name in
                      ('x', 'z', 'heading', 'counter', 'frame', 'deadline', 'courses', 'contacts')})
        trace[-1]['acceleration'] = actor['accumulator_a']
        start = state
    assert any(point['courses'] for point in trace), trace
    assert trace[-1]['acceleration'] > 0, trace[-1]
    assert any(point['x'] != 15120 or point['z'] != 15120 for point in trace), trace
    if blocked:
        radius = model['settings'][7]
        assert all(31 * 480 + radius <= point['x'] <= 32 * 480 - radius - 1 and
                   31 * 480 + radius <= point['z'] <= 32 * 480 - radius - 1 for point in trace)
    if repeated:
        assert probe.frontend.read(world + MAP_OFFSET + duplicate, 1) == bytes([model['objects'][index]['glyph']])
    return trace


def viewed_wake(probe, model, language):
    setup_open(probe, model)
    index = model['glyph_index'][0x81] - 1
    probe.ai(5)
    probe.sleeping(index)
    actor_pose(probe, model, index, 18000, 15000)
    probe.camera(0, 20000, -300, 15000, 90)
    probe.present()
    before = probe.capture(f'ai-sleeping-{language}')
    # Normal fixed-time simulation receives the preceding render's considered
    # image cache. This verifies the production seam, not just op1's call.
    for symbol in ('g_visual_probe', 'g_ai_probe'):
        probe.write(probe.symbols[symbol], struct.pack('<I', 0))
    probe.motion_step(0)
    for symbol in ('g_visual_probe', 'g_ai_probe'):
        probe.write(probe.symbols[symbol], struct.pack('<I', 1))
    awake = probe.ai_diag()
    assert awake['object'] == index and awake['state'] == 1 and awake['active'] == 1, awake
    assert probe.object_words(index)[1] != 65535
    after = probe.capture(f'ai-awake-{language}')
    assert before['faces'] > 0 and after['faces'] > 0
    return dict(language=language, live_consideration_path=True,
                valid_display_before_first_callback=True, before=before, after=after)


def combat_owners(probe, model):
    setup_open(probe, model)
    index = model['glyph_index'][0x81] - 1
    probe.ai(5)
    probe.sleeping(index)
    address = probe.symbols['g_render_world'] + 4 + index * 36
    health = probe.frontend.read(address + 16, 2)
    hero = model['player_index'] - 1
    probe.control(7, index, hero, 14)
    # Force a fresh snapshot without polling: AI update precedes combat update.
    awake = probe.ai(3, index, count=0)
    assert awake['state'] == 1 and awake['active'] == 1, awake
    assert probe.frontend.read(address + 16, 2) == health
    probe.control(8)
    original_flags = probe.frontend.read(address, 2)
    flags = struct.unpack('<H', original_flags)[0]
    probe.write(address, struct.pack('<H', flags | 0x400))
    spawned = probe.control(5, hero, ord('1'))
    assert spawned['result'] == 14, spawned
    removed = probe.ai(3, index, count=0)
    assert not removed['active'] and not removed['owners'], removed
    assert probe.slots()[0x81]['state'] == 1
    probe.control(6)
    probe.write(address, original_flags)
    return dict(sleeping_hit_woke_without_damage=True,
                reclaimed_monster_owner_removed_before_projectile_reuse=True,
                reused_glyph=0x81)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=Path('build/g14'))
    parser.add_argument('--core', type=Path, default=Path(os.environ['LOCALAPPDATA']) /
                        'Programs/mGBA/libretro-20261024/mgba_libretro.dll')
    parser.add_argument('--report', type=Path, default=Path('analysis/g14-ai-gba.json'))
    args = parser.parse_args()
    probe = AIProbe(args.core, args.build_dir)
    try:
        probe.until(lambda d:d['draws']>=1)
        probe.write(probe.symbols['g_camera_capture_hold'],struct.pack('<I',1))
        probe.until(lambda d: d['draws'] >= 3 and d['phase'] == 2)
        for symbol in ('g_motion_probe', 'g_visual_probe', 'g_combat_probe', 'g_ai_probe', 'g_camera_capture_hold'):
            probe.write(probe.symbols[symbol], struct.pack('<I', 1))
        perception_checks, traces, viewed = [], [], []
        for language in (0, 1):
            model = reset_level(probe, language)
            perception_checks.extend(perception(probe, model, language))
            index = model['glyph_index'][0x81] - 1
            first = motion_trace(probe, model, index, repeated=True)
            second = motion_trace(probe, model, index, repeated=True)
            assert first == second, (first, second)
            blocked = motion_trace(probe, model, index, blocked=True)
            traces.append(dict(language=language, repeat_equal=True, duplicate_glyph_preserved=True,
                               open=first, blocked=blocked))
            viewed.append(viewed_wake(probe, model, language))
        assert traces[0]['open'] == traces[1]['open'], 'Presentation language changed mechanics'
        owners = combat_owners(probe, model)
        stack = stack_usage(probe.frontend)
        assert stack['untouched_margin_bytes'] >= 512, stack
        report = dict(task='G14', status='passed', rom_sha256=sha(probe.rom),
                      script_sha256=sha(__file__), perception=perception_checks,
                      traces=traces, viewed_wake=viewed, combat_owners=owners,
                      stack=stack, physical_gba_tested=False,
                      whole_dos_session_equivalence=False)
        args.report.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
        print('G14 ARM perception edges, deterministic motion, blockage and duplicate glyph passed')
    finally:
        probe.close()


if __name__ == '__main__':
    main()
