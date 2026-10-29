"""Compare projectile contacts with an independent rational grid-segment oracle."""

import argparse
from fractions import Fraction
import json
import os
from pathlib import Path
import random
import re
import struct
import sys

ROOT = Path(__file__).resolve().parents[3]
for directory in ('ports/gba/scripts', 'tools/originals'):
    sys.path.append(str(ROOT / directory))
from profile_game_gba import GameProbe, freeze
from verify_combat_gba import setup_open, actor_pose
from verify_items_gba import MAP_OFFSET
from verify_gba import sha
from verify_sprites_gba import load_json


def segment_cells(start, end):
    """Partition a continuous segment at every grid boundary, sample intervals.

    Independent from the C crossing-order comparison; exact fractions ensure
    that merely touching a corner does not add either side cell.
    """
    times = {Fraction(0), Fraction(1)}
    for a, b in zip(start, end):
        if a != b:
            for line in range(min(a, b)//480*480, max(a, b)+481, 480):
                t = Fraction(line-a, b-a)
                if 0 < t < 1:
                    times.add(t)
    times = sorted(times)
    points = [Fraction(0), *((a+b)/2 for a, b in zip(times, times[1:])), Fraction(1)]
    result = []
    for t in points:
        x, z = (a + (b-a)*t for a, b in zip(start, end))
        cell = (int(x//480), int(z//480))
        if not result or result[-1] != cell:
            result.append(cell)
    return result


def trajectory(x, z, heading, weapon, elapsed, trig):
    distance = -(3000 if weapon == 1 else 3200)*elapsed//256
    whole = (-distance)//120
    remainder = distance + whole*120
    cells = [(x//480, z//480)]
    endpoints = set(cells)
    for step in [remainder] + [-120]*whole:
        end = (x + (trig[heading]*step//32768), z - (trig[361+heading]*step//32768))
        cells += segment_cells((x, z), end)
        endpoints.add((end[0]//480, end[1]//480))
        x, z = end
    return cells, endpoints


def cases():
    trig = list(map(int, re.findall(r'-?\d+', (ROOT/'ports/gba/src/renderer_trig.inc').read_text().split('{', 1)[1])))
    result = [dict(x=19918, z=14641, heading=71, weapon=2, ticks=416,
                   target=[40,31], wall=None, expected='hit', name='close-corner'),
              dict(x=19975, z=14621, heading=71, weapon=2, ticks=416,
                   target=[40,31], wall=None, expected='hit', name='back60'),
              dict(x=19220, z=15000, heading=90, weapon=2, ticks=416,
                   target=[40,31], wall=None, expected='hit', name='spawn-cell')]
    rng = random.Random(20261029)
    for n in range(160):
        x, z = 14000+rng.randrange(4000), 14000+rng.randrange(4000)
        heading, weapon = rng.randrange(360), 1+n%2
        ticks = (416, 832, 3328)[n%3]
        cells, endpoints = trajectory(x, z, heading, weapon, min(2048, ticks*256//416), trig)
        unique = list(dict.fromkeys(cells))
        interior = [c for c in unique[1:12] if c not in endpoints]
        target = interior[0] if interior else unique[min(3, len(unique)-1)]
        index = unique.index(target)
        wall = unique[index-1] if n%4 == 0 and index > 1 else None
        result.append(dict(x=x, z=z, heading=heading, weapon=weapon, ticks=ticks,
                           target=target, wall=wall, expected='wall' if wall else 'hit',
                           name=f'rational-{n}', skipped_endpoint_cell=target not in endpoints))
    # Exact corner grazes neither adjacent cell. Avoid both the source and
    # diagonal endpoint cell, which are valid contacts under the shared rules.
    for target in ([30,30], [31,31]):
        result.append(dict(x=14940, z=14820, heading=45, weapon=1, ticks=416,
                           target=target, wall=None, expected='clear', name='exact-corner'))
    return result


def capture(build, core, vectors):
    p = GameProbe(core, build)
    rows = []
    try:
        p.until(lambda d: d['draws'] >= 1)
        p.write(p.symbols['g_camera_capture_hold'], struct.pack('<I', 1))
        p.until(lambda d: d['draws'] >= 4 and d['phase'] == 2)
        freeze(p, True)
        model = load_json('research/evidence/runtime/ru/LEVEL1-runtime.json')
        world = p.symbols['g_render_world']
        wall_glyph = next(o['glyph'] for o in model['objects'] if o['class'] == 2)
        for language in (0, 1):
            p.language(language)
            for vector in vectors:
                setup_open(p, model)
                actor_pose(p, model, 0, vector['x'], vector['z'], vector['heading'])
                tx, tz = vector['target']
                actor_pose(p, model, 36, tx*480+240, tz*480+240)
                health = world+4+36*36+16
                p.write(health, struct.pack('<hh', 24, 1))
                if vector['wall']:
                    wx, wz = vector['wall']
                    p.write(world+MAP_OFFSET+4096+wz*64+wx, bytes([wall_glyph]))
                p.control(1, vector['weapon'], 100)
                p.control(3)
                slot, initial = p.active_slots()[0]
                d = p.poll(initial['last']+vector['ticks'])
                hp = struct.unpack('<h', p.frontend.read(health, 2))[0]
                observed = 'hit' if hp < 24 else 'wall' if d['misses'] else 'clear'
                rows.append(dict(language=language, **vector, observed=observed,
                                 health=hp, hits=d['hits'], misses=d['misses']))
        return dict(rom_sha256=sha(p.rom), rows=rows)
    finally:
        p.close()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--build-dir', type=Path, required=True)
    ap.add_argument('--reference-build', type=Path, required=True)
    ap.add_argument('--report', type=Path, required=True)
    args = ap.parse_args()
    os.chdir(ROOT)
    os.environ['DOK_PROBE_FAST_VIDEO'] = '1'
    core = Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll'
    vectors = cases()
    before = capture(args.reference_build, core, vectors)
    after = capture(args.build_dir, core, vectors)
    failures = [r for r in after['rows'] if r['observed'] != r['expected']]
    assert not failures, failures[:8]
    regressions = [r for r in before['rows'] if r['observed'] != r['expected']]
    assert any(r['name']=='close-corner' and r['observed']=='clear' for r in regressions)
    args.report.write_text(json.dumps(dict(status='passed', before=before, after=after,
        script_sha256=sha(__file__), core_sha256=sha(core), old_failures=len(regressions),
        scope='Controlled original actors/resources, empty-map fixtures; rational grid-segment '
              'oracle, walls/order/corner/spawn and416/832/3328 tick delays, RU/EN. '
              'Not the exact user session or physical GBA.'), indent=2)+'\n', encoding='utf-8')
    print('Projectile cell cases:',len(after['rows']),'old failures:',len(regressions),'new failures:0')


if __name__ == '__main__':
    main()
