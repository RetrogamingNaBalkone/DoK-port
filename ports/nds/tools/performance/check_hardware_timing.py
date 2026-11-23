"""Check the DS room pause panel against per-frame emulator history."""
import argparse
import json
import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import run_joint_frontend_emulator as joint

front = joint.front


class TimingObserver(joint.JointFrontendObserver):
    # The diagnostic replaces the lower pause screen. Check that LCD here.
    def check_pixels(self, p):
        if p[3] != 9:
            super().check_pixels(p)

    def observe_static(self, p):
        if p[3] != 9:
            super().observe_static(p)

    def read_panel(self):
        width, height, rgb = self.rgb()
        assert (width, height) == (256, 384)
        canvas = joint.render.ram(self, self.probe[21], 49152)
        assert set(canvas) == {0, 2}
        assert bytes(max(rgb[i:i+3]) > 127 for i in range(len(canvas)*3, len(rgb), 3)) == bytes(x == 2 for x in canvas)
        font = self.art.resources[1]['FONT8']
        glyphs = {font[c*8:c*8+8]: chr(c) for c in range(32, 127)}
        lines = []
        for y in (4, 17, 29, 40, 51, 62, 73, 84, 95, 106, 117, 128, 139, 149, 159, 173):
            chars = []
            for x in range(8, 248, 8):
                bits = bytes(sum((canvas[(y+dy)*256+x+dx] == 2) << (7-dx) for dx in range(8)) for dy in range(8))
                chars.append(glyphs[bits] if any(bits) else ' ')
            lines.append(''.join(chars).rstrip())
        return lines


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('target', type=Path)
    args = parser.parse_args()
    target = args.target.resolve()
    out = target/'hardware-timing'
    out.mkdir(exist_ok=True)
    f = TimingObserver(front.CORE, target, out)
    report = dict(core=front.transport.sha(front.CORE), rom=f.report['rom_sha256'], runs={})
    try:
        f.options['melonds_homebrew_sdcard'] = 'disabled'
        f.load(target/'dok_nds_gameplay.nds')
        report['options'] = {k: v.decode() for k, v in f.variables.items() if k in f.options}
        f.wait_for(1)
        for state in (2, 3):
            f.tap(front.A)
            f.wait_for(state)
        f.tap(front.A)
        f.text_to(7, 8)
        start = 0
        for name, keys, frames in [('turn', 1 << front.Y, 480), ('idle', 0, 120)]:
            if name == 'idle':
                start = len(f.report['history_records'])
                f.tap(front.START)
                f.wait_for(8)
            f.joypad = keys
            f.tick(frames)
            f.joypad = 0
            f.tap(front.START)
            f.wait_for(9)
            f.tick(8)
            lines = f.read_panel()
            rows = [r for r in f.report['history_records'][start:] if r['flags'] & 8][1:]
            def blanks(row):
                return row['vblank_end'] - row['vblank_start']
            groups = [[r for r in rows if (blanks(r) > 1) == late] for late in (False, True)]
            assert lines[0].startswith('DS room ')
            assert [len(g) for g in groups] == list(map(int, lines[2].split()[1:])), lines
            assert [sum(r['submitted_quads'] for r in g)//len(g) if g else 0 for g in groups] == list(map(int, lines[4].split()[2:])), lines
            profile = {r['frame_id']: r for r in f.report['profile_rows']}
            def stage_ms10(group,stage,peak=False):
                if not group:
                    return 0
                measurements = [profile[r['frame_id']] for r in group]
                hz = measurements[0]['timer_hz']
                samples = [r[stage] for r in measurements]
                ticks = max(samples) if peak else sum(samples)//len(samples)
                return (ticks*10000+hz//2)//hz
            for row,label,stage in (
                (3,'Peak work','cpu_excluding_wait'),(5,'Work','cpu_excluding_wait'),
                (6,'Simulation','simulation'),(7,'Geometry','geometry'),(8,'Sprites','sprites'),
                (9,'Discovery','discovery'),(10,'Effects','effects'),(12,'Map','map'),
            ):
                values = [stage_ms10(g,stage,peak=row==3) for g in groups]
                assert lines[row].split() == [*label.split(), *(f'{v//10}.{v%10}' for v in values)], lines
            assert [sum(bool(r['flags'] & 1) for r in g) for g in groups] == list(map(int, lines[13].split()[2:])), lines
            assert [max(map(blanks,g),default=0) for g in groups] == list(map(int, lines[15].split()[2:])), lines
            report['runs'][name] = lines
            f.capture(name)
        # Release the turn at a dense view. After key release and settling,
        # verify stable camera coordinates, then measure 120 VBlanks there.
        f.tap(front.START)
        f.wait_for(8)
        f.joypad = 1 << front.Y
        for _ in range(240):
            f.tick()
            if f.report['history_records'][-1]['submitted_quads'] >= 212:
                break
        else:
            raise AssertionError('Dense view not reached')
        f.joypad = 0
        f.tick(15)
        start = len(f.report['history_records'])
        f.tick(120)
        rows = f.report['history_records'][start:]
        poses = {(r['camera_x'], r['camera_z'], r['sin_q15'], r['cos_q15']) for r in rows}
        assert len(poses) == 1 and min(r['submitted_quads'] for r in rows) > 175
        costs = {r['frame_id']: r['cpu_excluding_wait']*1000/r['timer_hz'] for r in f.report['profile_rows']}
        report['dense_stationary'] = dict(frames=len(rows), pose=poses.pop(),
            quads=[min(r['submitted_quads'] for r in rows), max(r['submitted_quads'] for r in rows)],
            late=sum(r['vblank_end']-r['vblank_start'] > 1 for r in rows),
            work_ms=statistics.mean(costs[r['frame_id']] for r in rows))
        report['memory'] = joint.render.verify_memory(f, f.report['last_renderer'], target/'dok_nds_gameplay.elf')
    finally:
        f.close()
        front.transport.flush_native_stdio()
    (out/'result.json').write_text(json.dumps(report, indent=2)+'\n', 'utf8')
    print(json.dumps(report['runs']))


if __name__ == '__main__':
    main()
