"""Diagnostic split of submit CPU validation and FIFO emission; no FPS acceptance."""
import json
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import run_joint_cost_emulator as cost
from run_diag_emulator import elf_symbol
import run_renderer_emulator as render


class PipelineFrontend(cost.CostFrontend):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.pipeline_address, size = elf_symbol(self.target / 'dok_nds_gameplay.elf', 'g_nds_n23_submit_profile')
        assert size == (8 + 8 * 16) * 4
        self.pipeline_rows = {}
        try:
            self.relative_address, size = elf_symbol(self.target / 'dok_nds_gameplay.elf', 'g_nds_relative_matrix_probe')
            assert size == 128
        except ValueError:
            self.relative_address = None
        self.relative_audit = None

    def observe_map(self):
        super().observe_map()
        if self.relative_address:
            values = struct.unpack('<32I', render.ram(self, self.relative_address, 128))
            if values[0] == 0x234d4b44:
                self.relative_audit = dict(checks=values[2], mismatches=values[3],
                    translations=values[4], full_loads=values[5], emulator_only=True)
        raw = render.ram(self, self.pipeline_address, (8 + 8 * 16) * 4)
        values = struct.unpack('<136I', raw)
        if values[0] != 0x23534b44:
            return
        assert values[1] == 1 and values[3] == 33513982
        after = render.ram(self, self.pipeline_address, len(raw))
        for slot in range(8):
            row = values[8 + slot * 16:8 + (slot + 1) * 16]
            at = 32 + slot * 64 + 56
            if row[14] & 1 or raw[at:at + 4] != after[at:at + 4]:
                continue
            if not row[0] or row[0] > values[2]:
                continue
            assert (row[0] - 1) % 8 == slot
            fields = ('ordinal', 'camera_ticks', 'validation_ticks', 'initial_fence_ticks',
                      'emission_ticks', 'completion_ticks', 'final_fence_ticks', 'cells',
                      'quads', 'vertices', 'worst_case_fits', 'vertex_bound', 'camera_x',
                      'camera_z', 'sequence', 'reserved')
            item = dict(zip(fields, row, strict=True))
            if row[0] in self.pipeline_rows:
                assert self.pipeline_rows[row[0]] == item
            self.pipeline_rows[row[0]] = item

    def write_profile(self):
        super().write_profile()
        if not hasattr(self, 'pipeline_rows'):
            return
        expected = {row['frame_id'] for row in self.profile_report['rows']}
        report = dict(task='N23', acceptance_eligible=False,
            scope=__doc__, passed=bool(expected) and expected == self.pipeline_rows.keys(),
            missing=sorted(expected - self.pipeline_rows.keys()),
            extra=sorted(self.pipeline_rows.keys() - expected),
            observer=cost.joint.profile.transport.sha(Path(__file__)),
            rom_sha256=self.profile_report['rom_sha256'],
            elf_sha256=cost.joint.profile.transport.sha(self.target / 'dok_nds_gameplay.elf'),
            rows=list(self.pipeline_rows.values()))
        if self.relative_address:
            expected_checks = sum(row['cells'] for row in self.pipeline_rows.values())
            report['relative_matrix_audit'] = self.relative_audit
            report['relative_expected_checks'] = expected_checks
            report['passed'] = report['passed'] and bool(self.relative_audit) and (
                self.relative_audit['mismatches'] == 0 and self.relative_audit['checks'] == expected_checks)
        (self.target / 'pipeline-result.json').write_text(json.dumps(report, indent=2) + '\n', 'utf8')


def main():
    original = cost.CostFrontend
    cost.CostFrontend = PipelineFrontend
    try:
        return cost.main()
    finally:
        cost.CostFrontend = original


if __name__ == '__main__':
    raise SystemExit(main())
