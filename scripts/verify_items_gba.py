"""G12: independent scalar/item oracle against production ARM entry points.

Item effects come from a separate scanner of the original LEVEL bytes. Test
mailboxes invoke the same context and notification sink as live movement.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct

from analyze_levels import reference
from verify_gba import sha
from verify_renderer_gba import stack_usage
from verify_sprites_gba import Probe, load_json, original_animations

PLAYER_OFFSET = 17412
MAP_OFFSET = 17472


def signed(value):
    return (value + 32768) % 65536 - 32768


def baseline():
    words = [0] * 28
    words[:4] = [20, 7, 100, 100]
    words[4:10] = range(14, 20)
    words[10] = 64
    words[11:14] = [22, 23, 24]
    words[16:18] = [3, 12]
    return words + [0x12345678]


def stat_oracle(player, category, amount):
    result = player[:]
    amount = signed(amount)
    if category in (0, 1):
        current, maximum = signed(result[category]), signed(result[category + 2])
        if amount > 0 and current == maximum:
            return result, 0
        value = signed(current + amount)
        result[category] = (0 if value < 0 else min(value, maximum)) & 65535
    elif category in (2, 3):
        result[category] = (result[category] + amount) & 65535
        result[category - 2] = result[category]
    elif category == 10:
        result[10] |= amount & 65535
    elif category == 99:
        result[17] = (result[17] + amount) & 65535
    elif category == 200:
        result[27] = 1
    elif category in (*range(4, 10), 12, 13, 14):
        index = category if category <= 9 else category - 1
        result[index] = (result[index] + amount) & 65535
    else:
        return result, 0
    return result, 1


class ItemsProbe(Probe):
    def player(self):
        return list(struct.unpack('<28HI', self.frontend.read(
            self.symbols['g_render_world'] + PLAYER_OFFSET, 60)))

    def set_player(self, values):
        self.write(self.symbols['g_render_world'] + PLAYER_OFFSET,
                   struct.pack('<28HI', *values))

    def item_diag(self):
        names = 'magic error events kind object category value pickups'.split()
        return dict(zip(names, self.words('g_items_diag', 8)))

    def command(self, command, category=0, value=0):
        address = self.symbols['g_items_probe']
        request = self.words('g_items_probe', 2)[0] + 1
        previous = self.diag()['draws']
        self.write(address + 8, struct.pack('<Iii', command, category, value))
        self.write(address, struct.pack('<I', request))
        self.until(lambda d: self.words('g_items_probe', 2)[1] == request
                   and d['draws'] > previous and d['phase'] == 2)
        diag = self.item_diag()
        assert diag['magic'] == 0x4954454d and diag['error'] == 0, diag
        return self.words('g_items_probe', 6)[5]

    def pose(self, model, cell):
        obj = model['objects'][model['player_index'] - 1]
        actor = self.symbols['g_render_world'] + 9220 + obj['actor_index'] * 64
        self.write(actor + 4, struct.pack('<iii', cell % 64 * 480 + 240,
                                         -300, cell // 64 * 480 + 240))


def stats_checks(probe):
    cases = []
    for category in (0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 13, 14, 99, 200, 11, -1, 32767):
        for value in (-32768, -1, 0, 1, 32767):
            cases.append((baseline(), category, value))
    for category in (0, 1):
        for current, maximum, value in ((100, 100, 1), (100, 100, -1),
                                        (0, 100, -15), (32760, 32767, 10),
                                        (-32760, 100, -100), (1, -5, 0)):
            player = baseline()
            player[category], player[category + 2] = current & 65535, maximum & 65535
            cases.append((player, category, value))
    for category in (2, 3):
        player = baseline()
        player[category] = 32760
        cases.append((player, category, 20))
    checks = []
    for before, category, value in cases:
        probe.set_player(before)
        expected, accepted = stat_oracle(before, category, value)
        events = probe.item_diag()['events']
        actual = probe.command(1, category, value)
        assert actual == accepted and probe.player() == expected, (category, value, actual, probe.player(), expected)
        diag = probe.item_diag()
        assert diag['events'] == events + accepted, diag
        if accepted:
            assert diag['kind'] == (4 if category == 200 else 1) and diag['object'] == 65535, diag
            assert diag['category'] == category & 0xffffffff, diag
        checks.append(dict(category=category, value=value, accepted=accepted))
    return checks


def pickup_checks(probe, level, language, resources):
    model = load_json(f'build/runtime/ru/LEVEL{level + 1}-runtime.json')
    original = reference(resources[f'LEVEL{level + 1}'.encode().hex()])
    by_glyph = {obj['glyph']: obj for obj in original['objects']}
    original_map = bytes.fromhex(model['maps'][1])
    map_address = probe.symbols['g_render_world'] + MAP_OFFSET + 4096
    checks = []
    for index, obj in enumerate(model['objects']):
        if obj['class'] not in (3, 10):
            continue
        cells = [cell for cell, glyph in enumerate(original_map) if glyph == obj['glyph']]
        if not cells:
            continue
        fields = {offset: value for kind, offset, value in by_glyph[obj['glyph']]['fields'] if kind <= 2}
        category, value = fields[0x54], fields[0x56]
        cell = cells[0]
        probe.write(map_address, original_map)
        probe.pose(model, cell)
        player = baseline()
        probe.set_player(player)
        expected, accepted = stat_oracle(player, category, value)
        if obj['class'] == 10:
            slot = fields[0x81]
            expected[15] = slot
            expected[18 + slot] = index + 1
            expected[14] = slot
            accepted = 1
        before_objects = probe.frontend.read(probe.symbols['g_render_world'] + 4, 256 * 36)
        notifications = probe.item_diag()
        actual = probe.command(2)
        assert actual == accepted and probe.player() == expected, (level, language, obj['glyph'], probe.player(), expected)
        after_map = probe.frontend.read(map_address, 4096)
        expected_map = bytearray(original_map)
        if accepted:
            expected_map[cell] = 32
        assert after_map == expected_map, (level, obj['glyph'], 'shared glyph/map corruption')
        assert probe.frontend.read(probe.symbols['g_render_world'] + 4, 256 * 36) == before_objects, \
            'Pickup changed shared object flags or animation state'
        diag = probe.item_diag()
        assert diag['pickups'] == notifications['pickups'] + accepted, diag
        events = diag['events']
        assert probe.command(2) == 0 and probe.player() == expected
        assert probe.item_diag()['events'] == events, 'Repeated pickup emitted notifications'
        duplicate = False
        if obj['class'] == 10:
            other_index, other_obj = next((i, entry) for i, entry in enumerate(model['objects'])
                                          if entry['class'] == 10 and i != index)
            other_slot = next(field['number'] for field in other_obj['fields'] if field['offset'] == 0x81)
            owned = baseline()
            owned[18 + slot], owned[18 + other_slot] = index + 1, other_index + 1
            owned[14] = other_slot
            probe.set_player(owned)
            probe.write(map_address + cell, bytes([obj['glyph']]))
            duplicate_expected, _ = stat_oracle(owned, category, value)
            duplicate_expected[15] = slot
            assert probe.command(2) == 1 and probe.player() == duplicate_expected, \
                ('Duplicate weapon changed inventory/selection or skipped refill', probe.player(), duplicate_expected)
            duplicate = True
        checks.append(dict(level=level + 1, language=language, object=index,
                           glyph=obj['glyph'], category=category, value=value,
                           placements=len(cells), accepted=accepted, repeat_rejected=True,
                           other_cells_preserved=True, duplicate_weapon_verified=duplicate))
    return checks


def visible_live_pickup(probe, language):
    """Use a controlled placement and the production 416-tick movement path."""
    model = load_json('build/runtime/ru/LEVEL1-runtime.json')
    probe.camera(0, 15120, -300, 22800, 90)
    probe.language(language)
    obj_index = next(i for i, obj in enumerate(model['objects']) if obj['glyph'] == ord('8'))
    target = 47 * 64 + 30
    map_address = probe.symbols['g_render_world'] + MAP_OFFSET
    tmap = bytearray.fromhex(model['maps'][1])
    assert tmap[target] == 32
    tmap[target] = ord('8')
    probe.write(map_address + 4096, bytes(tmap))
    full = baseline()
    full[0] = full[2]
    probe.set_player(full)
    probe.pose(model, target)
    events = probe.item_diag()['events']
    assert probe.command(2) == 0 and probe.player() == full
    assert probe.item_diag()['events'] == events
    assert probe.frontend.read(map_address + 4096 + target, 1) == b'8', \
        'Full-health rejection consumed the item'
    probe.set_player(baseline())
    probe.pose(model, 47 * 64 + 31)
    # Direct diagnostic RAM writes may land after an in-progress draw already
    # sampled this cell. Wait for that draw and then capture a complete new one.
    probe.present()

    def object_face_count():
        count = probe.diag()['face_count']
        data = probe.frontend.read(probe.symbols['g_render_frame'] + 38408, count * 48)
        return sum(struct.unpack_from('<H', data, offset + 44)[0] == obj_index
                   for offset in range(0, len(data), 48))

    before = probe.capture(f'item-before-{language}')
    before_faces = object_face_count()
    assert before_faces > 0
    raw_before = probe.frontend.read(probe.symbols['g_render_frame'] + 4, 38400)
    mmap = bytearray.fromhex(model['maps'][0])
    player_glyph = model['objects'][model['player_index'] - 1]['glyph']
    mmap[:] = mmap.replace(bytes([player_glyph]), b' ')
    mmap[target] = player_glyph
    probe.write(map_address, bytes(mmap))
    probe.pose(model, target)
    notifications = probe.item_diag()
    mailbox = probe.symbols['g_motion_probe']
    request = probe.words('g_motion_probe', 3)[1] + 1
    draws = probe.diag()['draws']
    probe.write(mailbox + 12, struct.pack('<II', 0, 1))
    probe.write(mailbox + 4, struct.pack('<I', request))
    probe.until(lambda d: probe.words('g_motion_probe', 3)[2] == request
               and d['draws'] > draws and d['phase'] == 2)
    expected, accepted = stat_oracle(baseline(), 0, 15)
    assert accepted and probe.player() == expected, probe.player()
    assert probe.item_diag()['pickups'] == notifications['pickups'] + 1
    assert probe.frontend.read(map_address + 4096 + target, 1) == b' '
    after = probe.capture(f'item-after-{language}')
    assert object_face_count() == before_faces - 1, (language, before_faces, object_face_count())
    raw_after = probe.frontend.read(probe.symbols['g_render_frame'] + 4, 38400)
    changed = sum(a != b for a, b in zip(raw_before[12 * 240:132 * 240],
                                        raw_after[12 * 240:132 * 240]))
    assert changed > 0, 'Collected item remained visible in the ARM framebuffer'
    return dict(language=language, object=obj_index, cell=target,
                production_motion_path=True, full_positive_item_retained=True,
                changed_scene_pixels=changed,
                before=before, after=after)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=Path('build/g12'))
    parser.add_argument('--core', type=Path, default=Path(os.environ['LOCALAPPDATA']) /
                        'Programs/mGBA/libretro-20261024/mgba_libretro.dll')
    parser.add_argument('--report', type=Path, default=Path('analysis/g12-items-gba.json'))
    args = parser.parse_args()
    probe = ItemsProbe(args.core, args.build_dir)
    try:
        probe.until(lambda d: d['draws'] >= 3 and d['phase'] == 2)
        probe.write(probe.symbols['g_motion_probe'], struct.pack('<I', 1))
        probe.write(probe.symbols['g_visual_probe'], struct.pack('<I', 1))
        probe.write(probe.symbols['g_camera_capture_hold'], struct.pack('<I', 1))
        stats = stats_checks(probe)
        pickups, originals = [], []
        for level in range(3):
            probe.camera(level, 15120, -300, 22800, 90)
            for language in (0, 1):
                probe.language(language)
                resources, source = original_animations(language)
                originals.append(source)
                pickups.extend(pickup_checks(probe, level, language, resources))
        live = [visible_live_pickup(probe, language) for language in (0, 1)]
        stack = stack_usage(probe.frontend)
        assert stack['untouched_margin_bytes'] >= 512, stack
        assert len(pickups) >= 50, len(pickups)
        for source in originals:
            assert sha(source['path']) == source['sha256']
        report = dict(task='G12', status='passed', rom_sha256=sha(probe.rom),
                      script_sha256=sha(__file__), stats=stats, pickups=pickups,
                      live_pickup=live, stack=stack, originals=originals, physical_gba_tested=False,
                      whole_dos_session_equivalence=False)
        args.report.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
        print(f'G12 ARM: {len(stats)} stat vectors, {len(pickups)} real item effects and repeated pickup checks passed')
    finally:
        probe.close()


if __name__ == '__main__':
    main()
