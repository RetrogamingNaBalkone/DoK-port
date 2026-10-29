"""Live A-button point-blank shots against four original LEVEL1 bats, RU/EN.

Original maps/AI/resources remain active. Only the hero's pose/inventory/HP
are fixtures; this is not a navigation playthrough or physical GBA test.
"""

import argparse
import json
import os
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[3]
for directory in ('ports/gba/scripts', 'tools/originals'):
    sys.path.append(str(ROOT / directory))

from profile_game_gba import GameProbe, freeze, collect
from verify_combat_gba import actor_pose
from verify_items_gba import ItemsProbe
from verify_gba import sha
from verify_sprites_gba import load_json


def capture(build, core, ordinary_pickup=False):
    p = GameProbe(core, build)
    rows = []
    model = load_json('research/evidence/runtime/ru/LEVEL1-runtime.json')
    try:
        p.until(lambda d: d['draws'] >= 1)
        p.write(p.symbols['g_camera_capture_hold'], struct.pack('<I', 1))
        p.until(lambda d: d['draws'] >= 4 and d['phase'] == 2)
        freeze(p, True)
        for language in (0, 1):
            p.language(language)
            for target in (35, 36, 37, 38):
                definition = model['objects'][target]
                assert definition['animation'] == '8c9b989c5f8b'
                x, z = (definition['actor'][key] for key in ('x', 'z'))
                for weapon in (1, 2):
                    p.camera(1, 15120, -300, 22800, 90)
                    p.camera(0, x + 480, -300, z, 90)
                    world = p.symbols['g_render_world']
                    if ordinary_pickup:
                        glyph = ord('W') if weapon == 1 else ord('V')
                        at = bytes.fromhex(model['maps'][1]).index(glyph)
                        actor_pose(p, model, model['player_index'] - 1,
                                   at % 64 * 480 + 240, at // 64 * 480 + 240, 90)
                        assert ItemsProbe.command(p, 2) == 1
                        assert p.player()[14] == weapon
                    actor_pose(p, model, model['player_index'] - 1, x + 480, z, 90)
                    if not ordinary_pickup:
                        p.control(1, weapon, 1000)
                    player = p.player()
                    player[0] = player[2] = 32767
                    p.set_player(player)
                    health_address = world + 4 + target * 36 + 16
                    initial = struct.unpack('<h', p.frontend.read(health_address, 2))[0]
                    assert initial == 24, initial  # Accepted GBA balance profile.
                    p.write(p.symbols['test_camera'], b'\0')
                    freeze(p, False)
                    history = []
                    for frame in range(90):
                        timing = collect(p, 1, buttons=(8,))  # libretro A.
                        health, state = struct.unpack('<hh', p.frontend.read(health_address, 4))
                        combat = p.combat_diag()
                        history.append(dict(frame=frame, health=health, state=state,
                                            shots=combat['shots'], hits=combat['hits']))
                        assert combat['error'] == 0 and p.ai_diag()['error'] == 0
                        assert timing['samples'][0]['flow_state'] == 2
                        if not health:
                            break
                    assert health == 0, (language, target, weapon, history)
                    assert any(0 < item['health'] < initial for item in history)
                    assert combat['shots'] > 0
                    freeze(p, True)
                    p.write(p.symbols['g_camera_capture_hold'], struct.pack('<I', 1))
                    p.present()
                    rows.append(dict(language=language, target=target, glyph=definition['glyph'],
                                     target_start=[x, z], hero_start=[x+480, z], heading=90,
                                     weapon=weapon, initial_health=initial, final_health=health,
                                     frames_to_lethal=len(history), history=history))
        return dict(rom_sha256=sha(p.rom), rows=rows)
    finally:
        p.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--reference-build', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--ordinary-pickup', action='store_true',
                        help='Pick up the original weapon at its original TMap cell; retain ammo/show.')
    args = parser.parse_args()
    os.chdir(ROOT)
    os.environ['DOK_PROBE_FAST_VIDEO'] = '1'
    core = Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll'
    before = capture(args.reference_build, core, args.ordinary_pickup)
    after = capture(args.build_dir, core, args.ordinary_pickup)
    assert len(before['rows']) == len(after['rows']) == 16
    args.report.parent.mkdir(parents=True, exist_ok=True)
    result = dict(status='passed', script_sha256=sha(__file__), core_sha256=sha(core),
                  model_sha256=sha('research/evidence/runtime/ru/LEVEL1-runtime.json'),
                  before=before, after=after, cases_per_rom=16,
                  ordinary_weapon_pickup=args.ordinary_pickup,
                  scope='Live original LEVEL1 bats35..38, both weapons/locales; real held A, '
                        'hero placed480 units east and given HP32767. '
                        + ('Original weapon pickup/ammo/show at original TMap cells. ' if args.ordinary_pickup
                           else 'Direct equip with test ammo1000. ') +
                        'AI/maps/resources active; frame timing can alter AI poses. '
                        'Does not reproduce or dismiss the reported unregistered-hit scenario.')
    args.report.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print('Live close-range hits: all16 cases per ROM inflicted damage and killed the target')


if __name__ == '__main__':
    main()
