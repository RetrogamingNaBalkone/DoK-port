"""Plan N22 cardinal routes from pinned original runtime maps, never edit a world.

This is a navigation aid, not evidence of a completed playthrough. Dynamic
monsters, combat, source sensors, doors, inertia and actual collision remain
live and are exercised by the separate real-joypad emulator driver.
"""
from __future__ import annotations
import argparse
import hashlib
import heapq
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
OUT = ROOT / 'research/evidence/nds/n22'


def pin(path):
    return dict(path=path.relative_to(ROOT).as_posix(), bytes=path.stat().st_size,
                sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def field(obj, offset):
    return next((x['number'] for x in obj['fields'] if x['offset'] == offset), 0)


class Level:
    def __init__(self, number):
        self.number = number
        source = ROOT / f'research/evidence/runtime/ru/LEVEL{number}-runtime.json'
        oracle = ROOT / f'research/evidence/nds/n05/oracle/level{number}-lang1.json'
        self.data = d = json.loads(source.read_text('utf8'))
        nds = json.loads(oracle.read_text('utf8'))
        assert d['settings'] == nds['base']['settings']
        assert d['maps'] == nds['world']['maps']
        self.pins = [pin(source), pin(oracle)]
        self.width, _, self.height, self.side, *_ = d['settings']
        self.objects = {o['glyph']: o for o in d['objects']}
        self.maps = list(map(bytes.fromhex, d['maps']))
        hero = d['objects'][d['player_index'] - 1]['actor']
        self.start = hero['z'] // self.side * self.width + hero['x'] // self.side
        self.hero = hero
        self.keys = {i: field(self.objects[g], 0x56)
                     for i, g in enumerate(self.maps[1])
                     if self.objects[g]['class'] == 3 and field(self.objects[g], 0x54) == 10}
        self.weapons = {i for i, g in enumerate(self.maps[1]) if self.objects[g]['class'] == 10}
        self.exits = {i for i, g in enumerate(self.maps[2])
                      if self.objects[g]['class'] == 12 and field(self.objects[g], 0x66) == 1000}
        self.sensors = {i: field(self.objects[g], 0x66)
                        for i, g in enumerate(self.maps[2]) if self.objects[g]['class'] == 13}
        self.sensor_glyphs = sorted(set(self.sensors.values()))
        self.sensor_bits = {g: 1 << (8 + n) for n, g in enumerate(self.sensor_glyphs)}

    def xy(self, cell):
        return [cell % self.width, cell // self.width]

    def acquire(self, cell, flags, previous_cell=None):
        flags |= self.keys.get(cell, 0)
        if cell in self.weapons:
            flags |= 16
        if cell in self.sensors and previous_cell is not None and self.maps[2][cell]!=self.maps[2][previous_cell]:
            # Source previous_floor suppresses continuous same-glyph f cells;
            # a fresh entry toggles the remote door rather than granting a key.
            flags ^= self.sensor_bits[self.sensors[cell]]
        return flags

    def route(self, start=None, inventory=0, opened=(), required_weapon=True, targets=None, avoid=()):
        if start is None:
            start = self.start
        initial = (start, self.acquire(start, inventory))
        opened = set(opened)
        avoid = set(avoid)
        targets = self.exits if targets is None else set(targets)
        todo = [(0, initial)]
        distance = {initial: 0}
        previous = {}
        while todo:
            cost, current = heapq.heappop(todo)
            if cost != distance[current]:
                continue
            cell, flags = current
            if cell in targets and (flags & 16 or not required_weapon):
                result = [current]
                while current != initial:
                    current = previous[current]
                    result.append(current)
                result.reverse()
                return result, cost
            for nxt in (cell - 1, cell + 1, cell - self.width, cell + self.width):
                if not 0 <= nxt < self.width * self.height:
                    continue
                if sum(abs(a-b) for a, b in zip(self.xy(cell), self.xy(nxt))) != 1:
                    continue
                if nxt in avoid: continue
                actor, tile = (self.objects[self.maps[p][nxt]] for p in (0, 1))
                if not tile['flags'] & 16 and tile['class'] != 7:
                    continue
                if tile['class'] == 7:
                    key = field(tile, 0x5c)
                    sensor = self.sensor_bits.get(tile['glyph'], 0)
                    if key and not key & flags:
                        if sensor and not sensor & flags:continue
                        if not sensor and tile['glyph'] not in opened:continue
                if not actor['flags'] & 16 and actor['class'] not in (1, 9):
                    continue
                target = (nxt, self.acquire(nxt, flags, cell))
                # Avoid actors when possible, but never erase them. The live
                # driver must fight or wait if an actor blocks this route.
                hazard = 0
                if tile['class'] == 3 and field(tile, 0x54) == 0:
                    hazard = max(0, -field(tile, 0x56)) * 4
                floor = self.objects[self.maps[2][nxt]]
                if floor['class'] == 12 and field(floor, 0x66) == 0:
                    hazard += max(0, -field(floor, 0x68)) * 20
                hp=field(actor,0xa4)
                actor_cost=(4 if hp<=12 else 8 if hp<=24 else 16) if actor['class']==9 else 0
                new_cost = cost + 1 + hazard + actor_cost + (2 if tile['class'] == 7 else 0)
                if new_cost < distance.get(target, 1 << 60):
                    distance[target] = new_cost
                    previous[target] = current
                    heapq.heappush(todo, (new_cost, target))
        raise ValueError(f'LEVEL{self.number}: no route from {self.xy(start)} with flags {inventory}')

    def report(self):
        path, cost = self.route()
        rows = []
        last_direction = None
        for index, (cell, flags) in enumerate(path):
            x, z = self.xy(cell)
            t = self.objects[self.maps[1][cell]]
            feature = []
            if cell in self.keys: feature.append('key')
            if cell in self.weapons: feature.append('weapon')
            if cell in self.sensors: feature.append('sensor')
            if t['class'] == 7: feature.append('door')
            if cell in self.exits: feature.append('finish')
            direction = None if index + 1 == len(path) else tuple(a-b for a,b in zip(self.xy(path[index+1][0]), [x,z]))
            if index == 0 or feature or direction != last_direction or index + 1 == len(path):
                rows.append(dict(index=index, xy=[x,z], flags=flags, features=feature,
                                 glyph=self.maps[1][cell], floor=self.maps[2][cell]))
            last_direction = direction
        return dict(level=self.number, pins=self.pins, side=self.side,
                    address_system='Original recovered runtime world, row-major z; not LEVEL file row order',
                    hero=self.hero, cells=len(path), weighted_cost=cost,
                    final_keys=path[-1][1] & 15,
                    sensor_door_glyphs=self.sensor_glyphs,
                    key_cells=[dict(xy=self.xy(c), mask=k) for c,k in self.keys.items()],
                    sensor_cells=[dict(xy=self.xy(c), target=g) for c,g in self.sensors.items()],
                    path=[dict(xy=self.xy(c), flags=f) for c,f in path], waypoints=rows)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=Path, default=OUT / 'route-plan.json')
    args = p.parse_args()
    output = args.output.resolve()
    assert output.is_relative_to(OUT.resolve())
    levels = [Level(i).report() for i in (1,2,3)]
    report = dict(task='N22', planned=True, completed_playthrough=False,
                  method='Dijkstra cardinal graph; key masks, weapon pickup and sensor opening dependencies; actors penalized, never removed',
                  limitations=['Planning graph does not execute collision or combat',
                               'Static monster coordinates are initial placements only',
                               'Only successful real-input emulator FINISH can accept a route'],
                  planner=pin(Path(__file__)), levels=levels)
    output.parent.mkdir(parents=True,exist_ok=True)
    output.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n','utf8')
    for row in levels:
        print(f"LEVEL{row['level']}: {row['cells']} cells, {len(row['waypoints'])} waypoints, keys={row['final_keys']}, sensor targets={row['sensor_door_glyphs']}")

if __name__ == '__main__':
    main()
