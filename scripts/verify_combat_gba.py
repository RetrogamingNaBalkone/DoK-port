"""G13 production ARM weapon/projectile scenarios in mGBA.

Controlled poses/maps isolate arithmetic and lifecycle; a separate real A-button
scenario verifies the normal input path. This is not a whole DOS playthrough.
"""
import argparse
import hashlib
import os
from pathlib import Path
import struct
import json

from verify_gba import sha, scene_source_y
from verify_items_gba import ItemsProbe, baseline, MAP_OFFSET
from verify_sprites_gba import load_json
from verify_sprites_gba import original_animations
from probe_render_layout import frames
from verify_renderer_gba import stack_usage

DIAG_NAMES = ('magic error active shots removed hits misses events kind target value result '
              'selected pending charge health state shift frame resource now slot pstate '
              'object owner damage floor x y z deadline next previous').split()
SLOT_NAMES = ('x z deadline last y heading damage floor object owner resource frame '
              'next previous state glyph reserved').split()


class CombatProbe(ItemsProbe):
    def combat_diag(self):
        return dict(zip(DIAG_NAMES, self.words('g_combat_diag', len(DIAG_NAMES))))

    def control(self, op, a=0, b=0, c=0, tick=0, count=1, slot=0):
        address = self.symbols['g_combat_probe']
        request = self.words('g_combat_probe', 3)[1] + 1
        previous = self.diag()['draws']
        self.write(address + 12, struct.pack('<7I', *[
            value & 0xffffffff for value in (op, a, b, c, tick, count, slot)]))
        self.write(address + 4, struct.pack('<I', request))
        self.until(lambda d: self.words('g_combat_probe', 3)[2] == request
                   and d['draws'] > previous and d['phase'] == 2)
        diag = self.combat_diag()
        assert diag['magic'] == 0x434f4d42 and diag['error'] == 0, diag
        return diag

    def slots(self):
        # ARM has three borrowed pointers followed by the asserted slot40 array.
        data = self.frontend.read(self.symbols['g_combat'] + 12, 40 * 256)
        slots = [dict(zip(SLOT_NAMES, struct.unpack_from('<iiIIhhhh6HBBH', data, index * 40)))
                 for index in range(256)]
        for slot in slots:
            slot['state_flags'] = slot['state']
            slot['occupies_cell'] = bool(slot['state'] & 0x40)
            slot['state'] &= 15
        return slots

    def active_slots(self):
        return [(index, slot) for index, slot in enumerate(self.slots()) if slot['state']]

    def poll(self, tick, count=1, quantum=416):
        return self.control(4, quantum, tick=tick, count=count)

    def restart(self):
        return self.control(10, tick=10000)

    def motion_step(self, commands):
        address = self.symbols['g_motion_probe']
        request = self.words('g_motion_probe', 3)[1] + 1
        previous = self.diag()['draws']
        self.write(address + 12, struct.pack('<II', commands, 1))
        self.write(address + 4, struct.pack('<I', request))
        self.until(lambda d: self.words('g_motion_probe', 3)[2] == request
                   and d['draws'] > previous and d['phase'] == 2)


def setup_open(probe, model):
    """Empty two occupancy planes and keep all non-target actors off the ray."""
    world = probe.symbols['g_render_world']
    probe.control(6)
    probe.write(world + MAP_OFFSET, b' ' * 12288)
    for obj in model['objects']:
        if obj['actor_index'] != 65535:
            actor = world + 9220 + obj['actor_index'] * 64
            probe.write(actor + 4, struct.pack('<iii', 240, -300, 240))
    actor = world + 9220 + model['objects'][model['player_index'] - 1]['actor_index'] * 64
    probe.write(actor + 4, struct.pack('<iii', 20000, -300, 15000))
    probe.write(actor + 36, struct.pack('<h', 90))
    player_glyph = model['objects'][model['player_index'] - 1]['glyph']
    probe.write(world + MAP_OFFSET + (15000 // 480) * 64 + 20000 // 480, bytes([player_glyph]))
    player = baseline()
    player[0] = 100
    probe.set_player(player)
    probe.restart()
    probe.camera(0, 20000, -300, 15000, 90)


def ammo_and_flight(probe, model, language):
    checks = []
    for weapon, damage, floor, speed in ((1, 14, 7, 3000), (2, 4, 2, 3200)):
        setup_open(probe, model)
        probe.control(1, weapon, 0)
        before = probe.combat_diag()
        empty = probe.control(3)
        assert empty['result'] == 0 and empty['shots'] == before['shots'] and not empty['active']
        assert empty['charge'] == 0 and empty['kind'] == 6, empty
        probe.control(1, weapon, 1)
        fired = probe.control(3)
        assert fired['result'] == 1 and fired['charge'] == 0 and fired['active'] == 1, fired
        index, slot = probe.active_slots()[0]
        assert index == next(i for i, token in enumerate(model['glyph_index']) if not token)
        assert (slot['damage'], slot['floor'], slot['heading']) == (damage, floor, 90), slot
        assert slot['deadline'] == fired['now'] + 416
        assert slot['object'] == model['glyph_index'][ord('0') + weapon] - 1
        probe.poll(slot['deadline'])
        after = probe.slots()[index]
        assert (after['x'], after['z'], after['damage'], after['state']) == \
            (20000 - speed, 15000, (damage + floor) // 2, 1), after
        checks.append(dict(language=language, weapon=weapon, empty_rejected=True,
                           low_charge_shot=True, initial=slot, after_first_flight=after))
    return checks


def pool_checks(probe, model):
    setup_open(probe, model)
    expected_free = [index for index, token in enumerate(model['glyph_index']) if not token]
    full = probe.control(8)
    assert full['result'] == full['active'] == len(expected_free), full
    assert [index for index, _ in probe.active_slots()] == expected_free
    probe.control(1, 1, 100)
    failed = probe.control(3)
    assert failed['result'] == 0 and failed['charge'] == 100 and failed['shots'] == 0, failed
    assert failed['state'] == 2 and failed['kind'] == 7, failed
    first = expected_free[0]
    probe.control(9, first)
    probe.control(1, 1, 100)
    reused = probe.control(3)
    assert reused['result'] == 1 and reused['charge'] == 86
    assert probe.slots()[first]['state'] == 1 and reused['active'] == len(expected_free)
    cleared = probe.control(6)
    assert cleared['active'] == 0
    assert all(not slot['state'] and slot['next'] == slot['previous'] == 65535 for slot in probe.slots())
    return dict(capacity=len(expected_free), failed_allocation_preserved_charge=True,
                failed_allocation_changed_readiness=True, first_free_reused=first,
                all_events_unlinked_after_clear=True)


def actor_pose(probe, model, object_index, x, z, heading=90):
    obj = model['objects'][object_index]
    world = probe.symbols['g_render_world']
    address = world + 9220 + obj['actor_index'] * 64
    probe.write(address + 4, struct.pack('<iii', x, -300, z))
    probe.write(address + 36, struct.pack('<h', heading))
    if obj['class'] == 1:
        mmap = probe.frontend.read(world + MAP_OFFSET, 4096)
        probe.write(world + MAP_OFFSET, mmap.replace(bytes([obj['glyph']]), b' '))
    probe.write(world + MAP_OFFSET + (z // 480) * 64 + x // 480, bytes([obj['glyph']]))


def dynamic_occupancy(probe, model):
    setup_open(probe, model)
    probe.control(1, 1, 100)
    probe.control(3)
    index, slot = probe.active_slots()[0]
    probe.poll(slot['deadline'])
    slot = probe.slots()[index]
    assert slot['occupies_cell'], slot
    cell = slot['z'] // 480 * 64 + slot['x'] // 480
    occupancy_address = probe.symbols['g_combat'] + 12 + 40 * 256
    mask = probe.frontend.read(occupancy_address, 512)
    assert mask[cell // 8] & (1 << (cell % 8))
    boundary = slot['x'] // 480 * 480
    start = boundary - 60
    hero = model['player_index'] - 1
    actor_pose(probe, model, hero, start, slot['z'], 270)
    probe.motion_step(0x10)
    address = probe.symbols['g_render_world'] + 9220 + model['objects'][hero]['actor_index'] * 64
    blocked_x = struct.unpack('<i', probe.frontend.read(address + 4, 4))[0]
    assert blocked_x <= boundary - model['settings'][7] - 1, (start, blocked_x, boundary)
    probe.control(6)
    assert probe.frontend.read(occupancy_address, 512) == bytes(512)
    actor_pose(probe, model, hero, start, slot['z'], 270)
    probe.motion_step(0x10)
    free_x = struct.unpack('<i', probe.frontend.read(address + 4, 4))[0]
    assert free_x > blocked_x and free_x >= boundary, (blocked_x, free_x)
    return dict(projectile=index, cell=cell, blocked_x=blocked_x, after_clear_x=free_x,
                production_actor_collision_verified=True, clear_released_occupancy=True)


def collisions(probe, model):
    checks = []
    wall = next(obj for obj in model['objects'] if obj['class'] == 2)
    door_index, door = next((i, obj) for i, obj in enumerate(model['objects']) if obj['class'] == 7)
    monster = next(i for i, obj in enumerate(model['objects']) if obj['class'] == 9)
    world = probe.symbols['g_render_world']
    for kind in ('wall', 'door-closed', 'door-open', 'monster'):
        setup_open(probe, model)
        target_cell = 31 * 64 + 39
        if kind == 'monster':
            actor_pose(probe, model, monster, 18960, 15000)
            probe.write(world + 4 + monster * 36 + 16, struct.pack('<hh', 100, 1))
        else:
            glyph = wall['glyph'] if kind == 'wall' else door['glyph']
            probe.write(world + MAP_OFFSET + 4096 + target_cell, bytes([glyph]))
            if kind.startswith('door'):
                flags = model['objects'][door_index]['flags']
                flags = flags | 8 if kind == 'door-open' else flags & ~8
                probe.write(world + 4 + door_index * 36, struct.pack('<H', flags))
        probe.control(1, 1, 100)
        probe.control(3)
        index, initial = probe.active_slots()[0]
        hit = probe.poll(initial['deadline'])
        pending = probe.slots()[index]
        if kind == 'door-open':
            assert pending['state'] == 1 and pending['x'] == 17000 and hit['misses'] == 0, pending
            checks.append(dict(kind=kind, passed_through=True))
            continue
        assert pending['state'] == 2 and pending['resource'] == initial['resource'] and pending['frame'] == 0, pending
        assert pending['damage'] == 10 and pending['reserved'] != initial['resource'], pending
        if kind == 'monster':
            health = struct.unpack('<h', probe.frontend.read(world + 4 + monster * 36 + 16, 2))[0]
            assert health == 86 and hit['hits'] == 1 and hit['misses'] == 0, (health, hit)
        else:
            assert hit['misses'] == 1 and hit['hits'] == 0, hit
        probe.poll(pending['deadline'])
        effect = probe.slots()[index]
        assert effect['state'] == 3 and effect['resource'] == pending['reserved'] and effect['frame'] == 1, effect
        count = 0
        while probe.slots()[index]['state']:
            slot = probe.slots()[index]
            probe.poll(slot['deadline'])
            count += 1
            assert count < 30
        assert probe.combat_diag()['active'] == 0
        checks.append(dict(kind=kind, pending_flight_image=initial['resource'],
                           effect_resource=effect['resource'], effect_starts_at_frame=1,
                           damage_on_contact=14 if kind == 'monster' else None,
                           cleaned_up=True))
    # G15 connects the emitted notifications synchronously: wake sets state1,
    # damage sets hurt state3. Older standalone G13 ROMs had no AI listener.
    setup_open(probe, model)
    hero = model['player_index'] - 1
    probe.write(world + 4 + monster * 36 + 16, struct.pack('<hh', 100, 0))
    wake = probe.control(7, monster, hero, 14)
    assert wake['result'] == 1 and wake['kind'] == 10
    assert struct.unpack('<hh', probe.frontend.read(world + 4 + monster * 36 + 16, 4)) == (100,1 if 'g_ai_probe' in probe.symbols else 0)
    probe.write(world + 4 + monster * 36 + 16, struct.pack('<hh', 7, 1))
    death = probe.control(7, monster, hero, 14)
    assert death['kind'] == 9
    assert struct.unpack('<hh', probe.frontend.read(world + 4 + monster * 36 + 16, 4)) == (0,3 if 'g_ai_probe' in probe.symbols else 1)
    player = probe.player()
    immune = probe.control(7, hero, hero, 99)
    assert immune['result'] == 0 and probe.player() == player
    damaged = probe.control(7, hero, monster, 15)
    assert damaged['result'] == 1 and probe.player()[0] == 85
    checks.append(dict(kind='damage-events', sleeping_contact_wakes_without_damage=True,
                       dead_health_clamped=True, ai_notification_states_verified=True,
                       same_class_immune=True, player_health_after_15_damage=85))
    return checks


def weapon_transition(probe, model):
    setup_open(probe, model)
    probe.control(1, 2, 100)
    probe.control(1, 1, 100)
    requested = probe.control(2, 2)
    assert requested['result'] == 1 and requested['selected'] == 1 and requested['pending'] == 2
    tick = requested['now']
    expected = ((0, 1, 1), (0, 2, 1), (4, 3, 1), (1, 2, 2),
                (1, 1, 2), (5, 0, 2), (7, 0, 2))
    actual = []
    for step, target in enumerate(expected, 1):
        state = probe.poll(tick + step * 1100)
        observed = (state['state'], state['shift'], state['selected'])
        assert observed == target, (observed, target)
        actual.append(observed)
    invalid = probe.control(2, 8)
    assert invalid['result'] == 0 and invalid['selected'] == invalid['pending'] == 2
    return dict(states=actual, unavailable_weapon_rejected=True)


def first_pickup_show(probe, model):
    setup_open(probe, model)
    cell = 15000 // 480 * 64 + 20000 // 480
    probe.write(probe.symbols['g_render_world'] + MAP_OFFSET + 4096 + cell, b'W')
    assert probe.command(2) == 1
    assert probe.player()[14:16] == [1, 1]
    states = []
    for tick, expected in ((10000, (1, 2)), (11100, (1, 1)),
                           (12200, (5, 0)), (13300, (7, 0))):
        diag = probe.poll(tick)
        assert (diag['state'], diag['shift']) == expected, diag
        states.append(expected)
        if diag['state'] != 7:
            rejected = probe.control(3)
            assert rejected['result'] == 0 and rejected['charge'] == 150 and not rejected['active'], rejected
    fired = probe.control(3)
    assert fired['result'] == 1 and fired['charge'] == 136
    probe.control(6)
    return dict(ordinary_pickup_registered=True, show_states=states,
                fire_rejected_during_show=True, first_ready_shot_charge=136)


def live_input(probe, model):
    setup_open(probe, model)
    probe.control(1, 2, 100)
    probe.control(1, 1, 100)
    before = probe.combat_diag()['shots']
    probe.write(probe.symbols['g_combat_probe'], struct.pack('<I', 0))
    probe.write(probe.symbols['g_motion_probe'], struct.pack('<I', 0))
    probe.until(lambda d: probe.combat_diag()['shots'] > before, buttons=(8,))
    probe.write(probe.symbols['g_combat_probe'], struct.pack('<I', 1))
    probe.write(probe.symbols['g_motion_probe'], struct.pack('<I', 1))
    diag = probe.combat_diag()
    assert diag['shots'] == before + 1 and diag['charge'] == 86, diag
    shot = probe.capture('real-a-button')
    probe.control(6)
    probe.control(1, 1, 100)
    probe.write(probe.symbols['g_combat_probe'], struct.pack('<I', 0))
    probe.write(probe.symbols['g_motion_probe'], struct.pack('<I', 0))
    probe.until(lambda d: probe.combat_diag()['pending'] == 2, buttons=(10, 11))
    probe.until(lambda d: probe.combat_diag()['selected'] == 2 and probe.combat_diag()['state'] in (3, 7))
    before_laser = probe.combat_diag()['shots']
    probe.until(lambda d: probe.combat_diag()['shots'] > before_laser, buttons=(8,))
    probe.write(probe.symbols['g_combat_probe'], struct.pack('<I', 1))
    probe.write(probe.symbols['g_motion_probe'], struct.pack('<I', 1))
    laser = probe.combat_diag()
    delta = laser['shots'] - before_laser
    assert delta >= 1 and laser['charge'] == 100 - 4 * delta, laser
    laser_capture = probe.capture('real-l-r-cycle-and-laser-a')
    probe.control(6)
    return dict(button='A', gba_key_mask=1, native_fire_command=0x200,
                charge_after_shot=86, screenshot=shot,
                real_l_r_weapon_cycle=True, laser_shots=delta,
                laser_charge=laser['charge'], laser_screenshot=laser_capture)


def shot_series(probe, model):
    setup_open(probe, model)
    wall = next(obj['glyph'] for obj in model['objects'] if obj['class'] == 2)
    probe.write(probe.symbols['g_render_world'] + MAP_OFFSET + 4096 + 31 * 64 + 39, bytes([wall]))
    results = []
    for weapon, cost in ((1, 14), (2, 4)):
        probe.control(1, weapon, 1000)
        for _ in range(10):
            fired = probe.control(3)
            assert fired['result'] == 1, fired
            done = probe.poll(fired['now'] + 416, count=64)
            assert done['active'] == 0 and done['state'] in (3, 7), done
            assert all(not slot['state'] and slot['next'] == slot['previous'] == 65535
                       for slot in probe.slots())
            assert probe.frontend.read(probe.symbols['g_combat'] + 12 + 40 * 256, 512) == bytes(512)
        assert done['charge'] == 1000 - 10 * cost, done
        results.append(dict(weapon=weapon, shots=10, final_charge=done['charge'],
                            active_after_each_shot=0, all_events_and_occupancy_released=True))
    assert probe.combat_diag()['shots'] == probe.combat_diag()['removed'] == 20
    return results


def independent_instances(probe, model, language):
    setup_open(probe, model)
    probe.language(language)
    hero = model['player_index'] - 1
    probe.control(5, hero, ord('1'))
    probe.control(5, hero, ord('1'))
    active = probe.active_slots()
    assert len(active) == 2
    resources, _ = original_animations(language)
    animation = frames(resources[b'POPAL'.hex()])
    manifest = load_json('build/gba/assets/manifest.json')
    resource_id = next(entry['id'] for entry in manifest['resources'] if entry['name_hex'] == b'POPAL'.hex())
    slot_addresses = []
    for position, (index, _) in enumerate(active):
        address = probe.symbols['g_combat'] + 12 + index * 40
        slot_addresses.append(address)
        probe.write(address, struct.pack('<ii', 18400, 15000 + (-650 if position == 0 else 650)))
        probe.write(address + 16, struct.pack('<h', -250))
        probe.write(address + 28, struct.pack('<HH', resource_id, 2 if position == 0 else 3))
    captures = []
    for order in ((2, 3), (3, 2)):
        probe.present()
        for address, frame in zip(slot_addresses, order):
            probe.write(address + 30, struct.pack('<H', frame))
        probe.present()
        count = probe.diag()['face_count']
        data = probe.frontend.read(probe.symbols['g_render_frame'] + 38408, count * 48)
        faces = {struct.unpack_from('<H', data, offset + 44)[0]:
                 struct.unpack_from('<11iHh', data, offset)
                 for offset in range(0, len(data), 48)
                 if struct.unpack_from('<H', data, offset + 44)[0] >= 256}
        assert sorted(faces) == [256, 257], faces
        capture = probe.capture(f'independent-effects-{language}-{order[0]}')
        probe.frontend.run(2)
        actual=probe.frontend.colors555()
        palette=struct.unpack('<256H',probe.frontend.read(0x05000000,512))
        scene_objects=probe.words('g_hw_obj_diag',6)[5] if 'g_hw_obj_diag' in probe.symbols else 72
        compared = []
        for texture, frame in zip((256, 257), order):
            meta, pixels = animation[frame]
            x0, x1, top, _, bottom = faces[texture][:5]
            assert faces[texture][6:8] == (1600, 1600)
            step = ((meta['height'] - 1) << 16) // (bottom - top)
            checked = 0
            for x in range(max(0, x0 + (x0 & 1)), min(239, x1) + 1, 2):
                u = (meta['width'] - 1) * (x - x0) // (x1 - x0)
                for y in range(12,132):
                    sy=scene_source_y(y,scene_objects)
                    if sy<top or sy>bottom:continue
                    v = ((sy - top) * step) >> 16
                    color = pixels[v * meta['width'] + u]
                    if color:
                        assert actual[y * 240 + x] == actual[y * 240 + x + 1] == palette[color], \
                            (language, texture, frame, x, y, color, actual[y * 240 + x])
                        checked += 1
            assert checked > 20, checked
            compared.append(checked)
        captures.append(dict(frames=order, opaque_source_samples=compared, **capture))
    assert captures[0]['framebuffer_sha256'] != captures[1]['framebuffer_sha256']
    probe.control(6)
    return dict(language=language, template=active[0][1]['object'],
                independent_texture_slots=[256, 257], resource=resource_id,
                simultaneous_different_frames_verified=True, captures=captures)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=Path('build/g13'))
    parser.add_argument('--core', type=Path, default=Path(os.environ['LOCALAPPDATA']) /
                        'Programs/mGBA/libretro-20261024/mgba_libretro.dll')
    parser.add_argument('--report', type=Path, default=Path('analysis/g13-combat-gba.json'))
    args = parser.parse_args()
    probe = CombatProbe(args.core, args.build_dir)
    try:
        probe.until(lambda d:d['draws']>=1)
        probe.write(probe.symbols['g_camera_capture_hold'],struct.pack('<I',1))
        probe.until(lambda d: d['draws'] >= 3 and d['phase'] == 2)
        probe.write(probe.symbols['g_motion_probe'], struct.pack('<I', 1))
        probe.write(probe.symbols['g_visual_probe'], struct.pack('<I', 1))
        probe.write(probe.symbols['g_combat_probe'], struct.pack('<I', 1))
        # Isolate controlled combat from live AI polling/consideration.
        # Synchronous wake/hurt notifications remain active and checked.
        if 'g_ai_probe' in probe.symbols:
            probe.write(probe.symbols['g_ai_probe'],struct.pack('<I',1))
        probe.write(probe.symbols['g_camera_capture_hold'], struct.pack('<I', 1))
        model = load_json('build/runtime/ru/LEVEL1-runtime.json')
        flights = []
        for language in (0, 1):
            probe.language(language)
            flights.extend(ammo_and_flight(probe, model, language))
        pool = pool_checks(probe, model)
        instances = [independent_instances(probe, model, language) for language in (0, 1)]
        collision = collisions(probe, model)
        occupancy = dynamic_occupancy(probe, model)
        first_show = first_pickup_show(probe, model)
        transition = weapon_transition(probe, model)
        series = shot_series(probe, model)
        live = live_input(probe, model)
        stack = stack_usage(probe.frontend)
        assert stack['untouched_margin_bytes'] >= 512, stack
        report = dict(task='G13', status='passed', rom_sha256=sha(probe.rom),
                      script_sha256=sha(__file__), flights=flights, pool=pool,
                      independent_instances=instances, collisions=collision,
                      dynamic_occupancy=occupancy, weapon_transition=transition,
                      first_pickup_show=first_show, shot_series=series,
                      live_input=live, stack=stack,
                      whole_dos_session_equivalence=False, physical_gba_tested=False)
        args.report.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
        print('G13 ARM ammunition, free-flight arithmetic, native slot allocation and clear passed')
    finally:
        probe.close()


if __name__ == '__main__':
    main()
