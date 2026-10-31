"""Bounded native-input camera tour; the policy JSON never accesses emulator RAM.

Goals remain pending through resource alarms and real retries. A blocked goal
stops for review after four retries. This collects a sample, not a FINISH claim.
"""
import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ordinary
from run_routes import validate_review_choice
from plan_routes import Level
from analyze_tactics import pickup_lists
from read_live_ai import LiveAiReader


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--policy', type=Path, required=True)
    parser.add_argument('--health-threshold', type=int, default=0)
    parser.add_argument('--health-distance', type=int, default=25)
    parser.add_argument('--ai-layout', type=Path)
    args, remaining = parser.parse_known_args()
    policy = args.policy.resolve()
    stages = json.loads(policy.read_text('utf8'))
    assert isinstance(stages, list) and stages
    for stage in stages:
        validate_review_choice(stage | dict(checkpoint=1, level=3), 1, 3)
    original = ordinary.Observer

    class TourObserver(original):
        def turn_to(self, target, limit=100):
            from run_routes import Y,A,L
            for _ in range(limit):
                if self.pending_choice or not self.probe or self.probe[3]!=8:return
                angle=(target-self.heading()+180)%360-180
                if abs(angle)<=8:
                    self.game_step([])
                    return
                self.game_step([Y if angle>0 else A,L])
            raise AssertionError(('Bounded ordinary run-turn failed',target,self.last_pose))

        def turn_precise(self, target, limit=240):
            # This is a host input policy, not engine arithmetic. An 8-degree
            # camera intention avoids spending a life on one-degree alignment.
            return self.turn_to(target, limit=min(limit,100))

        def __init__(self, core, target, directory):
            super().__init__(core, target, directory)
            if args.ai_layout:
                self.ai_reader = LiveAiReader(target / 'dok_nds_gameplay.elf', args.ai_layout)
            # External review also uses this class's run-turn helper. Retain
            # its source before boot, even when no automatic policy is called.
            sources = [Path(__file__).resolve(), policy]
            if args.ai_layout:
                sources.append(args.ai_layout.resolve())
            self.report['tour_options'] = dict(health_threshold=args.health_threshold,
                health_distance=args.health_distance,host_turn_tolerance_degrees=8,host_turn_run=True)
            for source in sources:
                row = ordinary.pin(source)
                dest = self.directory / 'sources' / (row['sha256'] + '-' + source.name)
                dest.write_bytes(source.read_bytes())
                self.report.setdefault('tour_inputs', []).append(dict(source=row, snapshot=ordinary.pin(dest)))

        def policy_choice(self, saved):
            if not hasattr(self, 'tour_stage'):
                self.tour_stage = 0
                self.tour_blocks = 0
                self.detour_goal = None
            kind = saved['reason']['kind']
            stage = stages[self.tour_stage] if self.tour_stage < len(stages) else {}
            expected = ('goal_reached' if 'goal' in stage else
                        'wait_callbacks_complete' if 'wait_callbacks' in stage else 'manual_action_complete')
            detour_completed = kind == 'goal_reached' and saved['reason'].get('goal') == self.detour_goal
            if detour_completed or kind == 'native_retry':
                self.detour_goal = None
            if not detour_completed and kind == expected and ('goal' not in stage or saved['reason'].get('goal') == stage['goal']):
                self.tour_stage += 1
                self.tour_blocks = 0
            if kind == 'reviewed_path_block':
                self.tour_blocks += 1
            # These are actual inventory bits from a published native pause,
            # not a prediction that merely reaching a waypoint grants a key.
            while self.tour_stage < len(stages) and stages[self.tour_stage].get('key_mask',0) and (
                    saved['flow'][24] & stages[self.tour_stage]['key_mask']) == stages[self.tour_stage]['key_mask']:
                self.tour_stage += 1
                self.tour_blocks = 0
            if self.tour_stage == len(stages) or self.tour_blocks > 4:
                self.policy_end = 'bounded_tour_completed' if self.tour_stage == len(stages) else 'unresolved_blocked_goal'
                choice = dict(abort=True)
            elif kind == 'reviewed_path_block':
                choice = dict(goal=saved['reason']['goal'], combat=True, avoid_monsters=False,
                              pause_after_goal=True, health_reserve=0, ammo_reserve=0)
            else:
                choice = dict(combat=False, avoid_monsters=True, pause_after_goal=True,
                              health_reserve=0, ammo_reserve=0) | stages[self.tour_stage]
                if args.health_threshold and 0 < saved['flow'][6] < args.health_threshold:
                    data = Level(saved['level'])
                    data.maps[:2] = [bytes.fromhex(saved[key]) for key in ('mmap', 'tmap')]
                    x, _, z, _ = saved['pose']
                    start = int(z) // data.side * data.width + int(x) // data.side
                    inventory = saved['frontend'][50] | self.sensor_flags | (16 if saved['frontend'][51] else 0)
                    goals = {row['cell'] for row in pickup_lists(data)['health']
                             if data.maps[1][row['cell']] == row['glyph'] and row['cell'] != start}
                    actors = {cell for cell, glyph in enumerate(data.maps[0])
                              if data.objects[glyph]['class'] == 9 and cell != start}
                    try:
                        path, _ = data.route(start=start, inventory=inventory, targets=goals,
                                             required_weapon=False, avoid=actors)
                    except ValueError:
                        path = []
                    if path and len(path) <= args.health_distance:
                        self.detour_goal = data.xy(path[-1][0])
                        choice = dict(goal=self.detour_goal, combat=False, avoid_monsters=True,
                                      pause_after_goal=True, health_reserve=0, ammo_reserve=0)
                if not self.detour_goal:
                    choice['health_reserve'] = max(0, min(args.health_threshold - 1, saved['flow'][6] - 25))
            choice.pop('key_mask',None)
            self.report.setdefault('host_policy_choices', []).append(dict(checkpoint=saved['ordinal'],
                reason=kind, stage=self.tour_stage, ai_active=self.game[5], choice=choice.copy()))
            return choice

    ordinary.Observer = TourObserver
    sys.argv = ['ordinary.py', '--west-pressure', *remaining]
    return ordinary.main()


if __name__ == '__main__':
    raise SystemExit(main())
