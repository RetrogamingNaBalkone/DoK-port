"""N23 fresh-boot input replay, exact prefix proof, then reviewed native input.

The N22 replay verifier is derived only for the task name/output root. Every
recorded scalar row, map byte and native PAUSE descriptor must still match.
The optional AI reader observes the actual ELF; it never writes target state.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import types

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'ports/nds/tools/flow'))
sys.path.insert(0, str(ROOT / 'ports/nds/tools'))
from run_routes import RouteObserver, A, Y, L, pin
from read_live_ai import LiveAiReader


def main():
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument('--ai-layout', type=Path)
    args, remaining = parser.parse_known_args()
    source = ROOT / 'ports/nds/tools/flow/continue_prefix.py'
    raw = source.read_bytes()
    changes = []
    for old, new in ((b"ROOT/'build/nds/n22'", b"ROOT/'build/nds/n23'"),
                     (b"== 'N22'", b"== 'N23'"),
                     (b"task='N22'", b"task='N23'")):
        assert raw.count(old) == 1
        raw = raw.replace(old, new)
        changes.append(dict(old=old.decode(), new=new.decode(), occurrences=1))
    folder = ROOT / 'research/evidence/nds/n23/prefix-verifier'
    folder.mkdir(parents=True, exist_ok=True)
    derived = folder / (hashlib.sha256(raw).hexdigest() + '-continue_prefix.py')
    if derived.exists():
        assert derived.read_bytes() == raw
    else:
        derived.write_bytes(raw)
    module = types.ModuleType('n23_prefix_verifier')
    module.__file__ = str(derived)
    exec(compile(raw, str(derived), 'exec'), module.__dict__)

    class ReviewedObserver(RouteObserver):
        def __init__(self, core, target, directory):
            super().__init__(core, target, directory)
            self.report.update(task='N23', prefix_derivation=dict(source=pin(source),
                executed=pin(derived), changes=changes), max_naturally_active_ai=0)
            for path in (Path(__file__), source, derived, args.ai_layout):
                if path is None:
                    continue
                path = path.resolve()
                destination = directory / 'sources' / (pin(path)['sha256'] + '-' + path.name)
                destination.write_bytes(path.read_bytes())
                self.report.setdefault('continuation_inputs', []).append(dict(
                    original=pin(path), snapshot=pin(destination)))
            if args.ai_layout:
                self.ai_reader = LiveAiReader(target / 'dok_nds_gameplay.elf', args.ai_layout)

        def tick(self, count=1):
            result = super().tick(count)
            if self.game:
                self.report['max_naturally_active_ai'] = max(
                    self.report['max_naturally_active_ai'], self.game[5])
            return result

        def turn_to(self, target, limit=100):
            for _ in range(limit):
                if self.pending_choice or not self.probe or self.probe[3] != 8:
                    return
                angle = (target - self.heading() + 180) % 360 - 180
                if abs(angle) <= 8:
                    self.game_step([])
                    return
                self.game_step([Y if angle > 0 else A, L])
            raise AssertionError(('Bounded ordinary run-turn failed', target, self.last_pose))

        def turn_precise(self, target, limit=240):
            return self.turn_to(target, min(limit, 100))

    module.RouteObserver = ReviewedObserver
    sys.argv = ['continue_prefix.py', *remaining]
    return module.main()


if __name__ == '__main__':
    raise SystemExit(main())
