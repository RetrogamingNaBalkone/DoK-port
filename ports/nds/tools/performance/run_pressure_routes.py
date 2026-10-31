"""Saved native-input route with forward combat and observed actor coordinates.

This is a host player policy. The ROM, world, clocks and inventory are never
written. A target must first pass the existing published-map line-of-sight aid.
"""
import argparse
import importlib.util
import math
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'ports/nds/tools/flow'))
sys.path.insert(0, str(ROOT / 'ports/nds/tools'))
import route_profile
from read_live_ai import LiveAiReader


def main():
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument('--ai-layout', type=Path, required=True)
    args, remaining = parser.parse_known_args()
    install = route_profile.install_route_profiling

    def extend(cls):
        if getattr(cls, '_pressure_policy_extended', False):
            return install(cls)
        initialize = cls.__init__
        original_tick = cls.tick

        def init(self, core, target, directory):
            initialize(self, core, target, directory)
            self.ai_reader = LiveAiReader(target / 'dok_nds_gameplay.elf', args.ai_layout)
            for source in (Path(__file__).resolve(), args.ai_layout.resolve()):
                row = route_profile.pin(source)
                destination = directory / 'sources' / (row['sha256'] + '-' + source.name)
                destination.write_bytes(source.read_bytes())
                self.report.setdefault('pressure_policy_inputs', []).append(dict(
                    original=row, snapshot=route_profile.pin(destination)))

        def tick(self, count=1):
            result = original_tick(self, count)
            if self.game:
                self.report['max_naturally_active_ai'] = max(
                    self.report.get('max_naturally_active_ai', 0), self.game[5])
            return result

        def turn(self, target, limit=240):
            from run_routes import Y, A, L
            for _ in range(limit):
                if self.pending_choice or not self.probe or self.probe[3] != 8:
                    return
                angle = (target - self.heading() + 180) % 360 - 180
                if abs(angle) <= 1:
                    self.game_step([]); self.game_step([])
                    if abs((target - self.heading() + 180) % 360 - 180) <= 1:
                        return
                    continue
                button = Y if angle > 0 else A
                if abs(angle) > 30:
                    self.game_step([button, L])
                elif abs(angle) > 18:
                    self.game_step([button])
                else:
                    # Keep the accepted quiet-pulse alignment near a narrow
                    # corridor. Two real callbacks admit the physical release.
                    self.game_step([]); self.game_step([])
                    old = self.heading()
                    for _pulse in range(8):
                        self.game_step([button])
                        if self.heading() != old:
                            break
                    self.game_step([]); self.game_step([])
            raise AssertionError(('Bounded pressure policy turn', target, self.last_pose))

        def combat(self, data, dx, dz, buttons):
            from run_routes import aim_targets, Y, A, L, R
            if self.force_no_combat or not self.probe[49]:
                return [b for b in buttons if b != R]
            maps = self.map_snapshot(data)
            if not maps:
                return [b for b in buttons if b != R]
            sample = self.sample_ai(maps)
            exact = {e['glyph']: e for e in sample.get('enemies', [])
                     if sample.get('accepted') and e['occupancy_match'] and not e['corpse'] and e['health'] > 0}
            x, _, z, heading = self.last_pose
            targets = aim_targets(data, *maps, x, z, limit_cells=5, excluded_targets=self.dying_targets())
            if not targets:
                return buttons
            target = targets[0]
            actor = exact.get(target['glyph'])
            tx, tz = (actor['x'], actor['z']) if actor else tuple((q + .5) * data.side for q in target['xy'])
            wanted = math.degrees(math.atan2(x - tx, tz - z)) % 360
            angle = (wanted - heading + 180) % 360 - 180
            result = [b for b in buttons if b not in (Y, A, R)]
            if abs(angle) > 3:
                # Close aim uses released one-callback pulses to keep the
                # source turn acceleration from oscillating across the actor.
                if abs(angle) > 18 or self.flow[15] % 2:
                    result.append(Y if angle > 0 else A)
                if abs(angle) > 18 and L not in result:
                    result.append(L)
            else:
                result.append(R)
            return self.conserve_dying_fire(result, target)

        cls.__init__ = init
        cls.tick = tick
        cls.turn_to = cls.turn_precise = turn
        cls.assist_combat = combat
        cls._pressure_policy_extended = True
        install(cls)

    route_profile.install_route_profiling = extend
    spec = importlib.util.spec_from_file_location('pressure_saved_policy', Path(__file__).with_name('run_saved_routes.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    sys.argv = ['run_pressure_routes.py', *remaining]
    return module.main()


if __name__ == '__main__':
    raise SystemExit(main())
