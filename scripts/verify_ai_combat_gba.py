"""G15 independent ARM enemy attacks, damage and corpse scenarios.

Original RES animation frames and LEVEL fields supply the oracle. The release
balance policy remains explicit; this is not a whole DOS playthrough claim.
"""
import argparse
import json
import os
from pathlib import Path
import struct

from analyze_levels import reference
from probe_render_layout import frames
from verify_ai_gba import AIProbe, actor_values, reset_level
from verify_combat_gba import actor_pose
from verify_items_gba import MAP_OFFSET, baseline
from verify_renderer_gba import stack_usage
from verify_sprites_gba import original_animations, load_json
from verify_gba import sha

GROUPS = ((0, 0x80), (0, 0x81), (0, 0x8c), (0, 0xa0), (0, 0xa1),
          (1, 0x80), (1, 0xa1), (1, 0xa0), (2, 0x80), (2, 0xb3),
          (2, 0xa0), (2, 0xc6), (2, 0xd6), (2, 0xa1))


class AICombatProbe(AIProbe):
    def ai_diag(self):
        names = ('magic error steps wakes courses blocked contacts now owners '
                 'object state counter x z heading frame deadline active result '
                 'attacks hurts deaths completed melee ranged burst timer resource health flags').split()
        return dict(zip(names, self.words('g_ai_diag', len(names))))

    def refresh(self, index):
        return self.ai(3, index, count=0)

    def callback(self, index):
        state = self.refresh(index)
        assert state['active'], state
        return self.ai(3, index, tick=state['deadline'])


def source_data(language, level, glyph):
    resources, source = original_animations(language)
    model = reference(resources[f'LEVEL{level + 1}'.encode().hex()])
    obj = next(obj for obj in model['objects'] if obj['glyph'] == glyph)
    fields = {offset: value for _, offset, value in obj['fields']}
    animations = {kind: list(frames(resources[fields[offset]]))
                  for kind, offset in (('attack', 0xe8), ('death', 0xec))}
    return fields, animations, source


def scene(probe, model, initial, index, level, contact=True):
    probe.control(6)
    world = probe.symbols['g_render_world']
    probe.write(world, initial)
    probe.write(world + MAP_OFFSET, b' ' * 12288)
    for obj in model['objects']:
        if obj['actor_index'] != 65535:
            probe.write(world + 9220 + obj['actor_index'] * 64 + 4,
                        struct.pack('<iii', 240, -300, 240))
    actor_pose(probe, model, index, 15120, 15120, 270)
    actor_pose(probe, model, model['player_index'] - 1,
               15600 if contact else 17520, 15120, 90)
    player = baseline()
    player[0] = 100
    probe.set_player(player)
    probe.control(10, tick=10000)
    probe.ai(5, tick=10000)
    probe.sleeping(index)
    probe.camera(level, 17120, -300, 15120, 90)
    return probe.ai(2, index)


def melee_case(probe, model, initial, index, level, animation, resource):
    state = scene(probe, model, initial, index, level)
    steps = []
    for frame, (metadata, _) in enumerate(animation):
        state = probe.callback(index)
        assert state['state'] == 2 and state['frame'] == frame, state
        assert state['resource'] == resource, state
        assert probe.player()[0] == 100 and state['melee'] == 0, state
        assert state['deadline'] == (state['now'] + metadata['interval_ticks']) & 0xffffffff, state
        steps.append(dict(frame=frame, deadline=state['deadline']))
    state = probe.callback(index)
    assert probe.player()[0] == 95 and state['melee'] == 1, state
    assert state['state'] == 2 and state['frame'] == 0, state
    return dict(frames=steps, damage=5, hit_only_after_animation=True)


def death_case(probe, model, initial, index, level, animation, resource, occupied=False, capture=None):
    scene(probe, model, initial, index, level)
    world = probe.symbols['g_render_world']
    cell = 31 * 64 + 31
    if occupied:
        token = next(obj['glyph'] for obj in model['objects'] if obj['class'] == 4)
        probe.write(world + MAP_OFFSET + 4096 + cell, bytes([token]))
    else:
        token = 32
    probe.control(7, index, model['player_index'] - 1, 32767)
    state = probe.refresh(index)
    assert state['health'] == 0 and state['state'] == 3, state
    steps = []
    for frame, (metadata, _) in enumerate(animation):
        state = probe.callback(index)
        assert state['state'] == 4 and state['frame'] == frame, state
        assert state['active'] == 1 and not state['completed'], state
        assert state['resource'] == resource, state
        assert state['deadline'] == (state['now'] + metadata['interval_ticks']) & 0xffffffff, state
        steps.append(dict(frame=frame, deadline=state['deadline']))
    state = probe.callback(index)
    assert state['state'] == 4 and not state['active'] and not state['owners'], state
    assert state['completed'] == 1 and state['frame'] == len(animation) - 1, state
    assert state['flags'] & 16 and not state['flags'] & 0x400, state
    mmap = probe.frontend.read(world + MAP_OFFSET + cell, 1)[0]
    tmap = probe.frontend.read(world + MAP_OFFSET + 4096 + cell, 1)[0]
    glyph = model['objects'][index]['glyph']
    assert (mmap, tmap) == ((glyph, token) if occupied else (32, glyph)), (mmap, tmap)
    # No event remains to replay completion or move the persistent corpse.
    probe.ai(3, index, tick=state['now'] + 100000)
    again = probe.refresh(index)
    assert again['completed'] == 1 and not again['active'], again
    result = dict(frames=steps, completed_once=True, passable_persistent_corpse=True,
                  destination_plane=0 if occupied else 1, mmap=mmap, tmap=tmap)
    if capture:
        result['capture'] = probe.capture(capture)
    return result


def ranged_case(probe, model, initial, index, level, fields):
    delay, limit = fields[0xa8], fields[0xa6]
    threshold = 10000 + (delay + 1) * 10000
    scene(probe, model, initial, index, level, contact=False)
    early = probe.ai(3, index, tick=threshold - 1)
    assert not early['ranged'] and not probe.active_slots(), early
    scene(probe, model, initial, index, level, contact=False)
    first = probe.ai(3, index, tick=threshold)
    if not limit:
        assert not first['ranged'] and not first['burst'] and first['timer'] == threshold, first
        return dict(delay=delay, limit=limit, threshold_strict=True, zero_burst_disabled=True)
    assert first['ranged'] == first['burst'] == 1, first
    active = probe.active_slots()
    assert len(active) == 1, active
    projectile = active[0][1]
    expected_object = model['glyph_index'][fields[0xe7]] - 1
    assert projectile['object'] == expected_object and projectile['owner'] == index, projectile
    assert projectile['deadline'] == (first['now'] + 416) & 0xffffffff, projectile
    states = [first['burst']]
    for _ in range(limit - 1):
        state = probe.callback(index)
        states.append(state['burst'])
    assert states == list(range(1, limit + 1)), states
    reset = probe.callback(index)
    assert reset['burst'] == 0 and reset['timer'] == reset['now'], reset
    assert reset['ranged'] == limit and len(probe.active_slots()) == limit, reset
    return dict(delay=delay, limit=limit, threshold_strict=True, glyph=fields[0xe7],
                projectile_deadline_from_current_ai_time=True, series=states,
                reset_after_limit=True)


def damage_case(probe, model, initial, index, level):
    scene(probe, model, initial, index, level)
    probe.sleeping(index)
    before = probe.refresh(index)
    probe.control(7, index, model['player_index'] - 1, 1)
    woke = probe.refresh(index)
    assert woke['health'] == before['health'] and woke['state'] == 1, woke
    probe.control(7, index, model['player_index'] - 1, 1)
    hurt = probe.refresh(index)
    assert hurt['health'] == before['health'] - 1 and hurt['state'] == 3, hurt
    resumed = probe.callback(index)
    assert resumed['state'] == 2 and resumed['frame'] == 0, resumed
    return dict(sleeping_first_hit_only_wakes=True, second_hit_subtracts_one=True,
                hurt_state_three=True, contact_hurt_resumes_state_two=True)


def ranged_edges(probe, model, initial, index):
    world = probe.symbols['g_render_world']
    scene(probe, model, initial, index, 0, contact=False)
    wall = next(obj['glyph'] for obj in model['objects']
                if obj['class'] == 2 and not obj['flags'] & 0x18)
    for col in range(32, 36):
        probe.write(world + MAP_OFFSET + 4096 + 31 * 64 + col, bytes([wall]))
    blocked = probe.ai(3, index, tick=50000)
    assert not blocked['ranged'] and not blocked['burst'] and blocked['timer'] == 50000, blocked
    scene(probe, model, initial, index, 0, contact=False)
    other = next(i for i, obj in enumerate(model['objects'])
                 if obj['class'] == 9 and i != index and obj['actor_index'] != 65535)
    actor_pose(probe, model, other, 15600, 15120)
    shield = probe.ai(3, index, tick=50000)
    assert not shield['ranged'] and shield['timer'] == 50000, shield
    scene(probe, model, initial, index, 0, contact=False)
    full = probe.control(8)
    attempted = probe.ai(3, index, tick=50000)
    assert attempted['ranged'] == attempted['burst'] == 1, attempted
    assert len(probe.active_slots()) == full['active']
    probe.control(6)
    scene(probe, model, initial, index, 0, contact=False)
    probe.write(world + MAP_OFFSET + 31 * 64 + 31, b' ')
    actor_pose(probe, model, index, 13920, 15120, 270)
    spawned = probe.control(5, model['player_index'] - 1, ord('1'))
    assert spawned['result'] == 14
    shot = probe.active_slots()[0][1]
    probe.poll(shot['deadline'])
    shot = probe.active_slots()[0][1]
    assert shot['state'] == 1 and shot['occupies_cell'] and shot['x'] == 14520, shot
    projectile_front = probe.ai(3, index, tick=50000)
    assert not projectile_front['ranged'] and not projectile_front['burst'], projectile_front
    assert projectile_front['timer'] == 50000, projectile_front
    probe.control(6)
    return dict(solid_wall_blocks_sight=True, front_monster_suppresses_shot=True,
                failed_allocation_still_counts_attempt=True, pool_capacity=full['active'],
                published_projectile_front_suppresses_shot=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=Path('build/g15'))
    parser.add_argument('--core', type=Path, default=Path(os.environ['LOCALAPPDATA']) /
                        'Programs/mGBA/libretro-20261024/mgba_libretro.dll')
    parser.add_argument('--report', type=Path, default=Path('analysis/g15-ai-combat-gba.json'))
    parser.add_argument('--quick', action='store_true')
    args = parser.parse_args()
    probe = AICombatProbe(args.core, args.build_dir)
    resource_ids = {entry['name_hex']: entry['id'] for entry in load_json('build/gba/assets/manifest.json')['resources']}
    try:
        probe.until(lambda d: d['draws'] >= 3 and d['phase'] == 2)
        for symbol in ('g_motion_probe', 'g_visual_probe', 'g_combat_probe', 'g_ai_probe', 'g_camera_capture_hold'):
            probe.write(probe.symbols[symbol], struct.pack('<I', 1))
        checks = []
        extra = {}
        for language in ((0,) if args.quick else (0, 1)):
            current = None
            for group, (level, glyph) in enumerate(GROUPS[:1] if args.quick else GROUPS, 1):
                if level != current:
                    model = reset_level(probe, language, level)
                    initial = probe.frontend.read(probe.symbols['g_render_world'], 33864)
                    current = level
                index = model['glyph_index'][glyph] - 1
                fields, animations, source = source_data(language, level, glyph)
                melee = melee_case(probe, model, initial, index, level, animations['attack'], resource_ids[fields[0xe8]])
                hurt = damage_case(probe, model, initial, index, level)
                capture = f'corpse-{language}-E{group:02d}' if group in (1, 4, 5, 10) else None
                death = death_case(probe, model, initial, index, level, animations['death'], resource_ids[fields[0xec]], capture=capture)
                ranged = ranged_case(probe, model, initial, index, level, fields)
                if language == 0 and group == 1:
                    extra['ranged'] = ranged_edges(probe, model, initial, index)
                    extra['occupied_corpse'] = death_case(probe, model, initial, index, level,
                                                          animations['death'], resource_ids[fields[0xec]], occupied=True)
                assert sha(source['path']) == source['sha256']
                checks.append(dict(group=f'E{group:02d}', language=language, level=level + 1,
                                   glyph=glyph, melee=melee, hurt=hurt, death=death, ranged=ranged))
                print(f'ARM G15 language{language} E{group:02d} passed', flush=True)
        stack = stack_usage(probe.frontend)
        assert stack['untouched_margin_bytes'] >= 512, stack
        assert sha(probe.rom) == probe.metadata['rom_sha256'], 'ROM changed during ARM run'
        report = dict(task='G15', status='quick' if args.quick else 'passed',
                      rom_sha256=sha(probe.rom), script_sha256=sha(__file__),
                      groups=checks, extra=extra, stack=stack, physical_gba_tested=False,
                      whole_dos_session_equivalence=False)
        args.report.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
        print(f'G15 ARM {len(checks)} enemy variants passed')
    finally:
        probe.close()


if __name__ == '__main__':
    main()
