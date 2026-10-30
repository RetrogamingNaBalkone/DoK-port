"""N21P real attack/touch/pause gate using the accepted N20 combat route.

The full RU/EN joint runs provide native map pixels and accumulated coverage.
This separate input gate requires actual attack callbacks during drag and after
resume; it does not claim map raster acceptance by itself.
"""
import argparse
import json
from pathlib import Path
import sys

import run_joint_emulator as joint


class CombatFrontend(joint.JointFrontend):
    combat_proven = False

    def check_stage(self, name, probe):
        super().check_stage(name, probe)
        if name in ("live firing while dragging", "live drag with firing"):
            previous = self.map_report["stages"][-1]["probe"]
            assert probe[39] > previous[39], "Attack callbacks did not advance during touch"
        if name == "postresume clock and combat":
            paused = next(row["probe"] for row in self.map_report["stages"]
                          if row["name"] == "paused movement ignored")
            assert probe[39] > paused[39], "Attack callbacks did not resume"
            self.combat_proven = True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", type=Path, required=True)
    parser.add_argument("--language", type=int, choices=(1, 2), default=1)
    parser.add_argument("--packet", type=Path,
                        default=joint.ROOT / "build/nds/n21p/scene-combat-touch.dat")
    args = parser.parse_args()
    original, argv = joint.maps.MapFrontend, sys.argv
    try:
        joint.maps.MapFrontend = CombatFrontend
        sys.argv = [__file__, "--target", str(args.target), "--language", str(args.language),
                    "--packet", str(args.packet), "--input-only"]
        code = joint.maps.main()
        frontend = CombatFrontend.active_instance
        audio = json.loads((frontend.target / "audio/result.json").read_text("utf8"))
        report = dict(task="N21P", passed=code == 0 and frontend.combat_proven and audio["passed"],
                      scope=__doc__, combat_proven=frontend.combat_proven,
                      observer_sha256=joint.profile.transport.sha(Path(__file__)),
                      joint_observer_sha256=joint.profile.transport.sha(Path(joint.__file__)),
                      packet_sha256=joint.profile.transport.sha(args.packet),
                      rom_sha256=frontend.map_report["rom_sha256"],
                      observed_stages=len(frontend.map_report["stages"]),
                      input_report=(frontend.target / "map/result.json").relative_to(joint.ROOT).as_posix())
        frontend.profile_report["passed"] = report["passed"]
        frontend.write_profile()
        (frontend.target / "map/combat-result.json").write_text(json.dumps(report, indent=2) + "\n", "utf8")
        print(json.dumps(report))
        return 0 if report["passed"] else 1
    finally:
        joint.maps.MapFrontend, sys.argv = original, argv


if __name__ == "__main__":
    raise SystemExit(main())
