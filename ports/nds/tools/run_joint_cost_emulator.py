"""N21P diagnostic cost run for an explicit packet; never product acceptance.

Runs the unchanged gameplay binary through the joint observer. Missing full HUD
and touch-suite coverage is expected for a short packet. All lower-level runtime,
audio, native-pixel errors and exact stage coverage remain visible in reports.
"""
import json
from pathlib import Path

import run_joint_emulator as joint


class CostFrontend(joint.JointFrontend):
    def write_profile(self):
        self.profile_report['cost_observer_sha256'] = joint.profile.transport.sha(Path(__file__))
        self.profile_report['scope'] = __doc__
        self.profile_report['acceptance_eligible'] = False
        super().write_profile()


def main():
    original = joint.JointFrontend
    joint.JointFrontend = CostFrontend
    try:
        code = joint.main()
        f = CostFrontend.active_instance
        runtime = json.loads((f.target / 'emulator/result.json').read_text('utf8'))
        audio = json.loads((f.target / 'audio/result.json').read_text('utf8'))
        by_case = {}
        for row in f.profile_report['rows']:
            group = by_case.setdefault(row['case_id'], [])
            group.append(row)
        costs = {case: dict(frames=len(rows), mean_ms={
            key: sum(row[key] for row in rows) / len(rows) * 1000 / 33513982
            for key in ('cpu_excluding_wait', 'geometry', 'discovery', 'sprites',
                        'overlay', 'effects', 'submit_including_wait', 'hud',
                        'map', 'map_snapshot', 'map_raster', 'map_dma',
                        'geometry_build', 'geometry_cull', 'geometry_merge',
                        'geometry_other', 'geometry_doors') if key in rows[0]})
                 for case, rows in by_case.items()}
        report = dict(task='N21P', scope=__doc__, acceptance_eligible=False,
                      diagnostic_completed=bool(runtime['passed'] and audio['passed']
                          and not f.profile_report['missing_frames']),
                      accumulated_exit_code=code,
                      original_runtime_passed=runtime['passed'],
                      audio_passed=audio['passed'],
                      independent_map_error=f.map_report.get('error'),
                      independent_hud_error=f.hud_report.get('error'),
                      cases=costs, history=runtime.get('history'),
                      runtime_error=runtime.get('error'),
                      rom_sha256=f.profile_report['rom_sha256'],
                      expected_presentations=f.profile_report['expected_presentations'],
                      observed_presentations=len(f.profile_report['rows']))
        if report['independent_map_error'] or report['independent_hud_error']:
            report['diagnostic_completed'] = False
        (f.target / 'cost-result.json').write_text(json.dumps(report, indent=2)+'\n', 'utf8')
        print(json.dumps(report))
        return 0 if report['diagnostic_completed'] else 1
    finally:
        joint.JointFrontend = original


if __name__ == '__main__':
    raise SystemExit(main())
