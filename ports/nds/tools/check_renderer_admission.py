"""Run N07P capacity/range boundary tests on actual emulated ARM9/GE."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import struct

from benchmark_renderer import ROOT, OUT, build, digest
import run_diag_emulator as transport
from run_renderer_emulator import ram


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core", type=Path, required=True)
    args = parser.parse_args()
    build("admission", "render-check")
    target = OUT / "admission"
    frontend = transport.Frontend(args.core, target, "missing-media")
    report = dict(passed=False, scope="Actual emulated ARM9/GE; admission boundaries")
    try:
        assert digest(args.core) == "c7e6b58c667075a2a9868f2c3ba7a98e24200410a6da3051a7c555f1f0eef8ef"
        rom, elf = [target / ("dok_nds_admission." + ext) for ext in ("nds", "elf")]
        address, size = transport.elf_symbol(elf, "g_nds_admission_probe")
        assert size == 16
        frontend.load(rom)
        for frame in range(600):
            if frontend.frame_time and frontend.frame_time.callback:
                frontend.frame_time.callback(round(1000000/frontend.av.timing.fps))
            frontend.core.retro_run()
            words = struct.unpack("<4I", ram(frontend, address, size))
            if words[0] == 0x5041444b and words[3] == 1:
                report.update(checks=words[1], failures=words[2], frontend_frames=frame+1,
                              rom_sha256=digest(rom), elf_sha256=digest(elf))
                assert words[1] == 10 and words[2] == 0, words
                report["passed"] = True
                break
        assert report["passed"], "No completed admission probe"
    finally:
        frontend.close()
        path = ROOT / "research/evidence/nds/perf/admission-check.json"
        path.write_text(json.dumps(report, indent=2) + "\n", "utf8")
    print("ARM9 renderer admission: PASS (10 checks)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
