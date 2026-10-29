#!/usr/bin/env python3
"""N09 strict host/original-vector/ARM9 gate; N01-N08 stay read-only."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import time

from prepare_movement_fixture import (ROOT, OUT, EVIDENCE, INITIAL,
                                      INITIAL_SHA256, artifact, digest, safe_output)

CORE = [f"ports/nds/core/{name}.c" for name in
        ("contracts", "input", "controls", "motion", "camera", "time", "events")]
FLAGS = ["-std=c11", "-pedantic-errors", "-Wall", "-Wextra", "-Werror", "-O2", "-g",
         "-I", "ports/nds/include", "-I", "ports/nds/tests",
         "-I", "ports/nds/platform/movement_probe", "-I", "build/nds/n09/generated"]


def preserved_inputs() -> list[dict]:
    if digest(INITIAL) != INITIAL_SHA256:
        raise ValueError("Frozen initial N09 dependency manifest changed")
    initial = json.loads(INITIAL.read_text("utf8"))
    rows = initial.get("files", [])
    if not initial.get("passed") or initial.get("checked") != 282 or len(rows) != 282:
        raise ValueError("N09 requires all 282 original accepted dependency pins")
    seen = set()
    for row in rows:
        path = Path(row["path"])
        if not path.is_absolute():
            path = ROOT / path
        actual = artifact(path) if path.is_relative_to(ROOT) else dict(
            path=path.as_posix(), bytes=path.stat().st_size, sha256=digest(path))
        if actual != row:
            raise ValueError(f"Accepted N01-N08 input changed: {row['path']}")
        if row["path"] in seen:
            raise ValueError("Duplicate frozen dependency pin")
        seen.add(row["path"])
    return rows


def source_names() -> list[str]:
    names = set(CORE)
    for name in ("assets", "memory", "sha256", "world", "balance"):
        names.add(f"ports/nds/core/{name}.c")
    for name in ("scene", "geometry", "materials"):
        names.add(f"ports/nds/render/{name}.c")
    for name in ("input", "controls", "motion", "camera", "time", "events",
                 "contracts", "assets", "memory", "sha256", "world", "balance",
                 "geometry", "render_scene", "render_materials", "render_packet", "render_fixture"):
        names.add(f"ports/nds/include/dok/{name}.h")
    for name in ("input_cases.c", "input_cases.h", "test_input.c", "input_compile.c",
                 "motion_cases.c", "motion_cases.h", "test_motion.c", "motion_dump.c",
                 "movement_replay.c", "movement_replay.h", "test_movement_replay.c",
                 "movement_compile.c"):
        names.add(f"ports/nds/tests/{name}")
    for directory in ("input", "movement_probe"):
        for path in (ROOT / f"ports/nds/platform/{directory}").glob("*"):
            if path.suffix in (".c", ".h"):
                names.add(path.relative_to(ROOT).as_posix())
    for name in ("platform/time/clock.c", "platform/time/clock.h",
                 "platform/assets/file_reader.c", "platform/assets/file_reader.h",
                 "platform/render_probe/render_backend.c", "platform/render_probe/render_backend.h",
                 "platform/render_probe/render_probe.h", "Makefile.n09", "toolchain_versions.json"):
        names.add(f"ports/nds/{name}")
    for name in ("check_movement.py", "prepare_movement_fixture.py", "build_movement.ps1",
                 "audit_movement.py", "export_movement_evidence.ps1", "run_movement_emulator.py"):
        names.add(f"ports/nds/tools/{name}")
    names.add("tools/ghidra/ExportNdsMovement.java")
    names.add(INITIAL.relative_to(ROOT).as_posix())
    return sorted(names)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pins-only", action="store_true",
                        help="read-only dependency audit, not N09 readiness")
    parser.add_argument("--host-only", action="store_true", help="skip SDK build only")
    local = Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData/Local"))
    parser.add_argument("--zig", type=Path, default=local / "Programs/Zig/0.13.0/zig.exe")
    args = parser.parse_args()
    target = safe_output(EVIDENCE / ("preserved-inputs.json" if args.pins_only
                                    else "host-check.json"), EVIDENCE)
    report = dict(task="N09", passed=False, readiness_proven=False,
                  scope="dependency-pins-only" if args.pins_only else
                  "host-original-vectors" if args.host_only else "host-original-vectors-arm9-build",
                  python=sys.version, platform=platform.platform(), command=sys.argv,
                  commands=[], sources=[], artifacts=[], shared_suites={},
                  limitations=["Emulator execution is a separate gate; DS/DS Lite remain N24.",
                               "Linux x86_64-musl binaries are compiled/linked, not executed.",
                               "Free-flight diagnostic is not N10 collision verification.",
                               "Native x86 oracle stubs clocks/collision/keyboard boundaries; not a DOS session."])

    def run(argv: list[str]) -> str:
        print("Running:", subprocess.list2cmdline(argv), flush=True)
        entry = dict(argv=argv, cwd=str(ROOT))
        report["commands"].append(entry)
        start = time.monotonic()
        result = subprocess.run(argv, cwd=ROOT, capture_output=True, text=True,
                                encoding="utf8", errors="replace", timeout=600)
        entry.update(exit_code=result.returncode, seconds=time.monotonic() - start,
                     stdout=result.stdout, stderr=result.stderr)
        if result.stdout:
            print(result.stdout, end="", flush=True)
        if result.stderr:
            print(result.stderr, end="", file=sys.stderr, flush=True)
        if result.returncode:
            raise RuntimeError(f"N09 command failed with exit{result.returncode}: {argv[0]}")
        return result.stdout

    try:
        report["preserved"] = preserved_inputs()
        report["preserved_count"] = len(report["preserved"])
        if not args.pins_only:
            report["sources"] = [artifact(ROOT / name) for name in source_names()]
            version = run([str(args.zig), "version"]).strip()
            if version != "0.13.0":
                raise ValueError(f"Expected shared Zig0.13.0, got {version}")
            report["zig"] = dict(path=str(args.zig), version=version, sha256=digest(args.zig))
            run([sys.executable, "-B", "-X", "utf8", "ports/nds/tools/prepare_movement_fixture.py"])
            report["fixture"] = artifact(EVIDENCE / "fixture-report.json")
            suites = (("input", "test_input", ["input_cases.c"]),
                      ("motion", "test_motion", ["motion_cases.c"]),
                      ("replay", "test_movement_replay", ["movement_replay.c"]),
                      (None, "motion_dump", []))
            for key, name, shared in suites:
                sources = CORE + [f"ports/nds/tests/{item}" for item in shared] + [f"ports/nds/tests/{name}.c"]
                for suffix, extra in ((".exe", []), ("-linux-x86_64", ["-target", "x86_64-linux-musl", "-static"])):
                    output = safe_output(OUT / (name + suffix), OUT)
                    output.unlink(missing_ok=True)
                    run([str(args.zig), "cc", *FLAGS, *extra, *sources, "-o", str(output)])
                    report["artifacts"].append(artifact(output))
                if key:
                    suite = json.loads(run([str(OUT / (name + ".exe"))]))
                    if suite.get("failures") != 0 or suite.get("checks", 0) == 0:
                        raise ValueError(f"N09 {key} suite did not prove a passing nonempty run")
                    report["shared_suites"][key] = suite
            for name in ("input_compile", "movement_compile"):
                for label, extra in (("native", []), ("linux-x86_64", ["-target", "x86_64-linux-musl"])):
                    output = safe_output(OUT / f"{name}-{label}.o", OUT)
                    output.unlink(missing_ok=True)
                    run([str(args.zig), "cc", *FLAGS, *extra, "-c",
                         f"ports/nds/tests/{name}.c", "-o", str(output)])
                    report["artifacts"].append(artifact(output))
            run([sys.executable, "-B", "-X", "utf8", "ports/nds/tools/audit_movement.py",
                 "--probe", str(OUT / "motion_dump.exe")])
            original_path = EVIDENCE / "original-motion.json"
            original = json.loads(original_path.read_text("utf8"))
            if original.get("status") != "passed" or not original.get("host"):
                raise ValueError("N09 original comparison not established")
            report["original_vectors"] = artifact(original_path)
            if not args.host_only:
                run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                     "ports/nds/tools/build_movement.ps1", "-Rebuild"])
                build_path = OUT / "build-report.json"
                build = json.loads(build_path.read_text("utf-8-sig"))
                if not build.get("toolchain_lock_verified") or build.get("toolchain_lock_sha256") != digest(ROOT / "ports/nds/toolchain_versions.json"):
                    raise ValueError("N09 SDK package pins were not verified")
                expected = {f"build/nds/n09/dok_nds_movement.{ext}" for ext in ("nds", "elf", "map")}
                if {row["path"] for row in build.get("artifacts", [])} != expected:
                    raise ValueError("Unexpected N09 ARM9 artifact set")
                for row in build["artifacts"]:
                    if artifact(ROOT / row["path"]) != row:
                        raise ValueError("ARM9 artifact changed during N09 gate")
                report["arm9_build"] = dict(report=artifact(build_path), artifacts=build["artifacts"])
            run([sys.executable, "-B", "-X", "utf8", "ports/nds/tools/prepare_movement_fixture.py", "--check"])
            for row in report["sources"]:
                if artifact(ROOT / row["path"]) != row:
                    raise ValueError(f"Source changed during N09 gate: {row['path']}")
            for kind, value in (("source", dict(task="N09", files=report["sources"])),
                                ("build", dict(task="N09", artifacts=report["artifacts"] + report.get("arm9_build", {}).get("artifacts", [])))):
                manifest = safe_output(EVIDENCE / f"{kind}-manifest.json", EVIDENCE)
                manifest.write_text(json.dumps(value, indent=2) + "\n", encoding="utf8")
                report[f"{kind}_manifest"] = artifact(manifest)
        if preserved_inputs() != report["preserved"]:
            raise ValueError("Accepted pins changed during N09 gate")
        report["passed"] = True
    except Exception as error:
        report["error"] = f"{type(error).__name__}: {error}"
        print(report["error"], file=sys.stderr)
    target.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf8")
    print(f"N09 {report['scope']}: {'PASS' if report['passed'] else 'FAIL'}; {target}")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
