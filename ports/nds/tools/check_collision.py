#!/usr/bin/env python3
"""N10 strict host/original-vector/ARM9 gate; N01-N09 stay read-only."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import time

from prepare_collision_fixture import (ROOT, OUT, EVIDENCE, INITIAL,
                                      INITIAL_SHA256, artifact, digest, safe_output)

CORE = [f"ports/nds/core/{name}.c" for name in
        ("contracts", "input", "controls", "motion", "camera", "time", "events", "collision",
         "assets", "memory", "sha256", "world", "balance")]
CORE += [f"ports/nds/render/{name}.c" for name in ("scene", "geometry", "materials")]
CORE += ["ports/nds/platform/assets/file_reader.c"]
FLAGS = ["-std=c11", "-pedantic-errors", "-Wall", "-Wextra", "-Werror", "-O2", "-g",
         "-I", "ports/nds/include", "-I", "ports/nds/tests",
         "-I", "ports/nds/platform/assets", "-I", "ports/nds/platform/collision_probe",
         "-I", "build/nds/n10/generated"]


def preserved_inputs() -> list[dict]:
    if digest(INITIAL) != INITIAL_SHA256:
        raise ValueError("Frozen initial N10 dependency manifest changed")
    initial = json.loads(INITIAL.read_text("utf8"))
    rows = initial.get("files", [])
    if not initial.get("passed") or initial.get("checked") != 347 or len(rows) != 347:
        raise ValueError("N10 requires all 347 original accepted dependency pins")
    seen = set()
    for row in rows:
        path = Path(row["path"])
        if not path.is_absolute():
            path = ROOT / path
        actual = artifact(path) if path.is_relative_to(ROOT) else dict(
            path=path.as_posix(), bytes=path.stat().st_size, sha256=digest(path))
        if actual != row:
            raise ValueError(f"Accepted N01-N09 input changed: {row['path']}")
        if row["path"] in seen:
            raise ValueError("Duplicate frozen dependency pin")
        seen.add(row["path"])
    return rows


def source_names() -> list[str]:
    names = set(CORE)
    for path in (ROOT / "ports/nds/include/dok").glob("*.h"):
        names.add(path.relative_to(ROOT).as_posix())
    for name in ("collision_cases.c", "collision_cases.h", "test_collision.c",
                 "collision_routes.c", "collision_routes.h", "test_collision_routes.c",
                 "collision_compile.c", "collision_dump.c"):
        names.add(f"ports/nds/tests/{name}")
    for directory in ("input", "collision_probe", "time", "render_probe", "assets"):
        for path in (ROOT / f"ports/nds/platform/{directory}").glob("*"):
            if path.suffix in (".c", ".h"):
                names.add(path.relative_to(ROOT).as_posix())
    names.add("ports/nds/Makefile.n10")
    names.add("ports/nds/toolchain_versions.json")
    for name in ("check_collision.py", "prepare_collision_fixture.py", "build_collision.ps1",
                 "audit_collision.py", "export_collision_evidence.ps1", "run_collision_emulator.py"):
        names.add(f"ports/nds/tools/{name}")
    names.add("tools/ghidra/ExportNdsCollision.java")
    names.add(INITIAL.relative_to(ROOT).as_posix())
    return sorted(names)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pins-only", action="store_true",
                        help="read-only dependency audit, not N10 readiness")
    parser.add_argument("--host-only", action="store_true", help="skip SDK build only")
    local = Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData/Local"))
    parser.add_argument("--zig", type=Path, default=local / "Programs/Zig/0.13.0/zig.exe")
    args = parser.parse_args()
    target = safe_output(EVIDENCE / ("preserved-inputs.json" if args.pins_only
                                    else "host-check.json"), EVIDENCE)
    report = dict(task="N10", passed=False, readiness_proven=False,
                  scope="dependency-pins-only" if args.pins_only else
                  "host-original-vectors" if args.host_only else "host-original-vectors-arm9-build",
                  python=sys.version, platform=platform.platform(), command=sys.argv,
                  commands=[], sources=[], artifacts=[], shared_suites={},
                  limitations=["Emulator execution is a separate gate; DS/DS Lite remain N24.",
                               "Linux x86_64-musl binaries are compiled/linked, not executed.",
                               "Floor effects/slowdown and full hero gameplay remain outside this horizontal collision gate.",
                               "Native x86 oracle controls world fixtures; not a complete DOS game session."])

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
            raise RuntimeError(f"N10 command failed with exit{result.returncode}: {argv[0]}")
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
            run([sys.executable, "-B", "-X", "utf8", "ports/nds/tools/prepare_collision_fixture.py"])
            report["fixture"] = artifact(EVIDENCE / "fixture-report.json")
            suites = (("collision", "test_collision", ["collision_cases.c"]),
                      ("routes", "test_collision_routes", ["collision_routes.c"]),
                      (None, "collision_dump", []))
            for key, name, shared in suites:
                sources = CORE + [f"ports/nds/tests/{item}" for item in shared] + [f"ports/nds/tests/{name}.c"]
                for suffix, extra in ((".exe", []), ("-linux-x86_64", ["-target", "x86_64-linux-musl", "-static"])):
                    output = safe_output(OUT / (name + suffix), OUT)
                    output.unlink(missing_ok=True)
                    run([str(args.zig), "cc", *FLAGS, *extra, *sources, "-o", str(output)])
                    report["artifacts"].append(artifact(output))
                if key:
                    suite = json.loads(run([str(OUT / (name + ".exe"))] +
                                            ([str(OUT / "fixture/assets.ndp")] if key == "routes" else [])))
                    if suite.get("failures") != 0 or suite.get("checks", 0) == 0:
                        raise ValueError(f"N10 {key} suite did not prove a passing nonempty run")
                    report["shared_suites"][key] = suite
            for name in ("collision_compile",):
                for label, extra in (("native", []), ("linux-x86_64", ["-target", "x86_64-linux-musl"])):
                    output = safe_output(OUT / f"{name}-{label}.o", OUT)
                    output.unlink(missing_ok=True)
                    run([str(args.zig), "cc", *FLAGS, *extra, "-c",
                         f"ports/nds/tests/{name}.c", "-o", str(output)])
                    report["artifacts"].append(artifact(output))
            run([sys.executable, "-B", "-X", "utf8", "ports/nds/tools/audit_collision.py",
                 "--probe", str(OUT / "collision_dump.exe")])
            original_path = EVIDENCE / "original-collision.json"
            original = json.loads(original_path.read_text("utf8"))
            if (original.get("status") != "passed" or not original.get("probe") or
                len(original.get("reports", [])) != 6 or
                any(not row.get("comparison") for row in original["reports"])):
                raise ValueError("N10 original comparison not established")
            report["original_vectors"] = artifact(original_path)
            if not args.host_only:
                run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                     "ports/nds/tools/build_collision.ps1", "-Rebuild"])
                build_path = OUT / "build-report.json"
                build = json.loads(build_path.read_text("utf-8-sig"))
                if not build.get("toolchain_lock_verified") or build.get("toolchain_lock_sha256") != digest(ROOT / "ports/nds/toolchain_versions.json"):
                    raise ValueError("N10 SDK package pins were not verified")
                expected = {f"build/nds/n10/dok_nds_collision.{ext}" for ext in ("nds", "elf", "map")}
                if {row["path"] for row in build.get("artifacts", [])} != expected:
                    raise ValueError("Unexpected N10 ARM9 artifact set")
                for row in build["artifacts"]:
                    if artifact(ROOT / row["path"]) != row:
                        raise ValueError("ARM9 artifact changed during N10 gate")
                report["arm9_build"] = dict(report=artifact(build_path), artifacts=build["artifacts"])
            run([sys.executable, "-B", "-X", "utf8", "ports/nds/tools/prepare_collision_fixture.py", "--check"])
            for row in report["sources"]:
                if artifact(ROOT / row["path"]) != row:
                    raise ValueError(f"Source changed during N10 gate: {row['path']}")
            for kind, value in (("source", dict(task="N10", files=report["sources"])),
                                ("build", dict(task="N10", artifacts=report["artifacts"] + report.get("arm9_build", {}).get("artifacts", [])))):
                manifest = safe_output(EVIDENCE / f"{kind}-manifest.json", EVIDENCE)
                manifest.write_text(json.dumps(value, indent=2) + "\n", encoding="utf8")
                report[f"{kind}_manifest"] = artifact(manifest)
        if preserved_inputs() != report["preserved"]:
            raise ValueError("Accepted pins changed during N10 gate")
        report["passed"] = True
    except Exception as error:
        report["error"] = f"{type(error).__name__}: {error}"
        print(report["error"], file=sys.stderr)
    target.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf8")
    print(f"N10 {report['scope']}: {'PASS' if report['passed'] else 'FAIL'}; {target}")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
