"""Check renderer supersession, fresh host suites and optimized N11 runtime.

Frozen N07/N11 reports and binaries remain historical evidence, never rewritten.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys

from benchmark_renderer import ROOT, OUT, build, digest
from check_interactions import CORE, FLAGS

EVIDENCE = ROOT / "research/evidence/nds/perf"
ARCHIVED = {
    "ports/nds/platform/render_probe/render_backend.c": "render_backend-before.c",
    "ports/nds/render/scene.c": "scene-before.c",
    "ports/nds/platform/interactions_probe/main.c": "interactions-main-before.c",
    "ports/nds/tools/run_interactions_emulator.py": "run_interactions_emulator-before.py",
    "ports/nds/render/door_render.c": "door_render-before.c",
    "ports/nds/Makefile.n11": "Makefile.n11-before",
    "ports/nds/tools/audit_collision.py": "audit_collision.py.before",
    "ports/nds/tools/audit_time.py": "audit_time.py.before",
    "ports/nds/tools/audit_movement.py": "audit_movement.py.before",
    "ports/nds/tools/audit_interactions.py": "audit_interactions.py.before",
    "ports/nds/tools/audit_geometry.py": "audit_geometry.py.before",
    "ports/nds/tools/audit_evidence.py": "audit_evidence.py.before",
}


def artifact(path):
    return dict(path=path.relative_to(ROOT).as_posix(), bytes=path.stat().st_size, sha256=digest(path))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core", type=Path, required=True)
    parser.add_argument("--zig", type=Path, default=Path.home()/"AppData/Local/Programs/Zig/0.13.0/zig.exe")
    parser.add_argument("--audit", action="store_true", help="also hash the full world/queue around every render")
    args = parser.parse_args()
    variant = "n11-audit" if args.audit else "n11-optimized"
    target = OUT / variant
    gate_path = EVIDENCE / ("host-check-audit.json" if args.audit else "host-check.json")
    target.mkdir(parents=True, exist_ok=True)
    old = json.loads((ROOT / "research/evidence/nds/n11/host-check.json").read_text("utf8"))
    gate = dict(task="N07P", passed=False, commands=[], preserved_count=0, superseded=[], sources=[], shared_suites={})
    def run(argv):
        result = subprocess.run(list(map(str, argv)), cwd=ROOT, capture_output=True, text=True, encoding="utf8")
        gate["commands"].append(dict(argv=list(map(str, argv)), exit_code=result.returncode,
                                     stdout=result.stdout, stderr=result.stderr))
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        return result.stdout
    try:
        # Preserve original provenance. A hash exception is tied to exact
        # archived bytes; arbitrary dependency changes are never accepted.
        for row in old["preserved"] + old["sources"] + old["arm9_build"]["artifacts"]:
            path = ROOT / row["path"]
            if digest(path) != row["sha256"]:
                archived = EVIDENCE / ARCHIVED[row["path"]]
                assert digest(archived) == row["sha256"], "Historical source archive differs"
                gate["superseded"].append(dict(original=row, archived=artifact(archived), current=artifact(path)))
            else:
                gate["preserved_count"] += 1
        assert run([args.zig, "version"]).strip() == "0.13.0"
        suites = (("interactions", "test_interactions", ["interaction_cases.c"]),
                  ("door_render", "test_door_render", ["door_render_cases.c"]),
                  ("router", "test_interaction_router", ["interaction_router_cases.c"]),
                  ("scene", "test_render_scene", []))
        for key, name, shared in suites:
            exe = target / (name + ".exe")
            sources = CORE + [f"ports/nds/tests/{s}" for s in shared] + [f"ports/nds/tests/{name}.c"]
            run([args.zig, "cc", *FLAGS, *sources, "-o", exe])
            output = run([exe])
            if key == "scene":
                gate["scene_checks"] = json.loads(output)
                assert gate["scene_checks"]["passed"] and gate["scene_checks"]["checks"]
            else:
                suite = json.loads(output)
                assert suite["checks"] and suite["failures"] == 0
                gate["shared_suites"][key] = suite
        # Compare the complete phase-zero door pass with the original base
        # scene on every occupied source camera pose, including merge IDs.
        exe = target / "door_render_dump.exe"
        run([args.zig, "cc", *FLAGS, *CORE, "ports/nds/tests/door_render_dump.c", "-o", exe])
        gate["static_scene_regression"] = []
        for level in (1, 2, 3):
            result = json.loads(run([exe, ROOT/"build/nds/n11/fixture/assets.ndp", str(level), "--scan", "1"]))
            assert result["passed"] and result["checks"]
            gate["static_scene_regression"].append(result)
        build(variant, "n11", audit=args.audit)
        (target / "fixture").mkdir(exist_ok=True)
        for name in ("assets.ndp", "scene.dat"):
            shutil.copyfile(ROOT / "build/nds/n11/fixture" / name, target / "fixture" / name)
        names = {row["path"] for row in old["sources"]}
        names.update("ports/nds/tools/" + name for name in ("benchmark_renderer.py", "check_render_performance.py"))
        names.add("ports/nds/tools/requirements.txt")
        gate["sources"] = [artifact(ROOT / name) for name in sorted(names)]
        gate["arm9_build"] = dict(artifacts=[artifact(target / ("dok_nds_interactions." + ext)) for ext in ("nds", "elf", "map")])
        gate["passed"] = True
    finally:
        gate_path.write_text(json.dumps(gate, indent=2) + "\n", "utf8")
    command = [sys.executable, "-B", "-X", "utf8", ROOT / "ports/nds/tools/run_interactions_emulator.py",
               "--core", args.core, "--rom", target / "dok_nds_interactions.nds",
               "--elf", target / "dok_nds_interactions.elf", "--host-gate", gate_path,
               "--output-root", target]
    result = subprocess.run(list(map(str, command)), cwd=ROOT)
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
