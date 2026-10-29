#!/usr/bin/env python3
"""N08 strict host/original-vector/ARM9 gate; accepted N01–N07 stay read-only."""
from __future__ import annotations

import argparse
import hashlib
import json
from fractions import Fraction
import os
from pathlib import Path
import platform
import random
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / "build/nds/n08"
EVIDENCE = ROOT / "research/evidence/nds/n08"
PRIOR_SUMMARIES = (
    "n01-evidence", "n02-platform", "n03-architecture", "n04-assets",
    "n05-balance", "n06-visual", "n07-renderer",
)


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def artifact(path: Path) -> dict:
    name = path.relative_to(ROOT).as_posix() if path.is_relative_to(ROOT) else path.as_posix()
    return dict(path=name,
                bytes=path.stat().st_size, sha256=digest(path))


def safe_output(path: Path, root: Path) -> Path:
    """Reject traversal, aliases and hardlinks before creating any output."""
    path = path.absolute()
    if ".." in path.parts or path == root or not path.is_relative_to(root):
        raise ValueError("N08 output escapes its fixed root")
    for item in (path, *path.parents):
        if item == ROOT:
            break
        if item.is_symlink() or item.is_junction():
            raise ValueError(f"N08 output crosses alias: {item}")
    if path.exists() and (not path.is_file() or path.stat().st_nlink != 1):
        raise ValueError(f"N08 output is not an unaliased regular file: {path}")
    path.parent.mkdir(parents=True, exist_ok=True)
    return path


def preserved_inputs() -> list[dict]:
    """Check declared accepted artifacts and recurse through source manifests."""
    found: dict[str, dict] = {}

    def pin(row: dict) -> None:
        path = Path(row["path"])
        path = path if path.is_absolute() else ROOT / path
        actual = artifact(path)
        if actual["sha256"] != row["sha256"] or (
                "bytes" in row and actual["bytes"] != row["bytes"]):
            raise ValueError(f"Accepted previous input changed: {actual['path']}")
        previous = found.get(actual["path"])
        if previous and previous != actual:
            raise ValueError(f"Conflicting accepted pins: {actual['path']}")
        if previous:
            return
        found[actual["path"]] = actual
        if path.name == "source-manifest.json":
            walk(json.loads(path.read_text("utf8")))

    def walk(value) -> None:
        if isinstance(value, dict):
            if isinstance(value.get("path"), str) and isinstance(value.get("sha256"), str):
                pin(value)
            if isinstance(value.get("source_manifest"), str) and value.get("source_manifest_sha256"):
                pin(dict(path=value["source_manifest"], sha256=value["source_manifest_sha256"]))
            if isinstance(value.get("report"), str) and value.get("report_sha256"):
                pin(dict(path=value["report"], sha256=value["report_sha256"]))
            for child in value.values():
                walk(child)
        elif isinstance(value, list):
            for child in value:
                walk(child)

    for name in PRIOR_SUMMARIES:
        path = ROOT / f"analysis/nds-{name}.json"
        # Summary prose may evolve; accepted referenced hashes may not.
        found[path.relative_to(ROOT).as_posix()] = artifact(path)
        walk(json.loads(path.read_text("utf8")))
    return sorted(found.values(), key=lambda row: row["path"])


def numerical_vectors(executable: Path) -> dict:
    """Independent exact arithmetic, including the C overflow fallback path."""
    generator = random.Random(0x4E3038)
    frequencies = [(33513982, 64), (1193182, 119), (1, 1),
                   (0xFFFFFFFF, 1), (1, 0xFFFFFFFF),
                   (0xFFFFFFFF, 0xFFFFFFFF), (17, 29)]
    frequencies += [(generator.randrange(1, 1 << 32), generator.randrange(1, 1 << 32))
                    for _ in range(41)]
    checks = 0
    transcripts = []
    for index, (hz_num, hz_den) in enumerate(frequencies):
        ratio = Fraction(1193182 * hz_den, 119 * hz_num)
        baseline = 0 if index % 2 else 123456789
        initial = 0xFFFFFF00 if index % 3 == 0 else 17
        state = dict(last=baseline, fraction=0, ticks=initial, paused=0)
        commands = [("R", baseline), ("R", baseline + 1),
                    ("R", baseline + 100000), ("P", baseline + 200000),
                    ("P", 0), ("R", baseline + 300000),
                    ("U", baseline + 400000), ("U", 0),
                    ("R", baseline + 500000), ("R", 0),
                    ("R", (1 << 63) + 12345), ("R", (1 << 64) - 2),
                    ("P", (1 << 64) - 1), ("U", (1 << 64) - 1),
                    ("R", (1 << 64) - 1)]
        expected = []
        for action, current in commands:
            status, output = 0, 0xA55AA55A
            idempotent = (action == "P" and state["paused"]) or (action == "U" and not state["paused"])
            if not idempotent:
                if current < state["last"]:
                    status = 3
                else:
                    if not state["paused"]:
                        whole, state["fraction"] = divmod(
                            (current - state["last"]) * ratio.numerator + state["fraction"],
                            ratio.denominator)
                        state["ticks"] = (state["ticks"] + whole) & 0xFFFFFFFF
                    state["last"] = current
                    if action == "P": state["paused"] = 1
                    if action == "U": state["paused"] = 0
                    if action == "R": output = state["ticks"]
            expected.append((status, output, state["last"], state["fraction"],
                             state["ticks"], state["paused"], ratio.numerator, ratio.denominator))
        protocol = f"{hz_num} {hz_den} {initial} {baseline}\n"
        protocol += "".join(f"{action} {value}\n" for action, value in commands)
        run = subprocess.run([str(executable)], input=protocol, capture_output=True,
                             text=True, encoding="utf8", timeout=30, check=True)
        actual = [tuple(map(int, line.split())) for line in run.stdout.splitlines()]
        if actual != expected:
            raise AssertionError(f"N08 numerical vector differs at frequency {hz_num}/{hz_den}: {actual} != {expected}")
        checks += len(expected) * 8
        transcripts.append(dict(frequency=[hz_num, hz_den], rows=len(expected),
                                input_sha256=hashlib.sha256(protocol.encode()).hexdigest()))
    return dict(passed=True, cases=len(frequencies), steps=len(frequencies)*15,
                scalar_checks=checks, seed="0x4e3038", vectors=transcripts,
                model="Python arbitrary-precision divmod; uint32 native tick reduction only")


def original_vectors(executable: Path) -> dict:
    path = ROOT / "research/evidence/nds/n08/original-vectors.json"
    oracle = json.loads(path.read_text("utf8"))
    results = []
    for language in ("ru", "en"):
        for case in oracle["samples"][language]["cases"]:
            lines = []
            for callback, action in case.get("actions", {}).items():
                lines.append(f"A {callback} {action.get('advance', 0)} {int(action.get('once', False))}")
                for operation in action.get("operations", []):
                    code = {"cancel": 1, "interval": 2, "schedule": 3, "destroy": 4}[operation[0]]
                    values = [int(value) & 0xFFFFFFFF for value in operation[1:]]
                    values += [0] * (4-len(values))
                    lines.append(f"B {callback} {code} " + " ".join(map(str, values)))
            for operation in case["operations"]:
                name, *values = operation
                if name == "now": lines.append(f"N {values[0] & 0xFFFFFFFF}")
                elif name == "dispatch": lines.append("D")
                elif name == "clear": lines.append("K")
                elif name == "destroy": lines.append(f"X {values[0]}")
                elif name == "schedule":
                    lines.append("S " + " ".join(str(value & 0xFFFFFFFF) for value in values))
                else: raise ValueError(f"Unknown original vector operation {name}")
            lines.append("END")
            protocol = "\n".join(lines)+"\n"
            run = subprocess.run([str(executable)], input=protocol, capture_output=True,
                                 text=True, encoding="utf8", timeout=30)
            if run.returncode:
                raise AssertionError(f"N08 original vector {language}/{case['name']} adapter failed: exit{run.returncode}, {run.stderr}")
            actual = dict(callbacks=[], final=dict(owners=[]))
            reads = []
            current = None
            for line in run.stdout.splitlines():
                kind, *words = line.split(); values = list(map(int, words))
                if kind == "C": actual["callbacks"].append(dict(zip(("owner","callback","clock"),values)))
                elif kind == "R": reads.append(values[0])
                elif kind == "H": actual["final"].update(zip(("cursor","owner_count","event_count"),values))
                elif kind == "O":
                    current = dict(id=values[0],count=values[1],events=[])
                    actual["final"]["owners"].append(current)
                elif kind == "E" and current is not None:
                    current["events"].append(dict(zip(("deadline","interval","callback","parameter"),values)))
                else: raise ValueError("Unexpected N08 vector adapter output")
            expected = json.loads(json.dumps(dict(callbacks=case["callbacks"],final=case["final"])))
            if not expected["final"]["owner_count"]:
                expected["final"]["cursor"] = 0 # native stale cursor is outside active ring
            excluded = []
            if case["name"] == "destroy_other_active":
                # Original destroy frees owner1's event array without clearing
                # its count. Bytes beyond its lifetime are not semantic state.
                excluded = [1]
                for value in (actual, expected):
                    value["final"]["owners"] = [row for row in value["final"]["owners"] if row["id"] != 1]
            if actual != expected:
                raise AssertionError(f"N08 original vector differs at {language}/{case['name']}: {actual} != {expected}")
            expected_reads = [row["value"] for row in case["clock_reads"]]
            if reads != expected_reads:
                raise AssertionError(f"N08 actual injected clock samples differ at {language}/{case['name']}: {reads} != {expected_reads}")
            results.append(dict(language=language,name=case["name"],passed=True,
                                callbacks=len(actual["callbacks"]),excluded_destroyed_owner_ids=excluded,
                                clock_reads=reads,result=actual,input_sha256=hashlib.sha256(protocol.encode()).hexdigest()))
    return dict(passed=True, oracle=artifact(path), cases=len(results), results=results,
                limitations=["Only native dispatch(0) one-sweep vectors are compared; nonzero busy-wait durations are outside the NDS API.",
                             "Destroyed native owner allocation bytes and stale empty-ring cursor are excluded from active state comparison."])


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pins-only", action="store_true",
                        help="read-only dependency audit; never establishes N08 readiness")
    parser.add_argument("--host-only", action="store_true", help="skip ARM9 SDK build only")
    local = Path(os.environ.get("LOCALAPPDATA", Path.home()/"AppData/Local"))
    parser.add_argument("--zig", type=Path, default=local/"Programs/Zig/0.13.0/zig.exe")
    args = parser.parse_args()
    target = safe_output(EVIDENCE / ("preserved-inputs.json" if args.pins_only else "host-check.json"), EVIDENCE)
    report = dict(task="N08", passed=False, scope="dependency-pins-only" if args.pins_only
                  else "host-original-vectors" if args.host_only else "host-original-vectors-arm9-build",
                  readiness_proven=False, python=sys.version, platform=platform.platform(),
                  command=sys.argv, commands=[], sources=[], artifacts=[],
                  limitations=["Emulator execution is a separate gate; physical DS/DS Lite remain N24.",
                               "Linux x86_64-musl executables are compiled and linked, not executed.",
                               "N02 has no source manifest; declared artifacts/reproducibility and later pinned transport are verified."])

    def run(argv: list[str]) -> str:
        print("Running:", subprocess.list2cmdline(argv), flush=True)
        entry = dict(argv=argv,cwd=str(ROOT)); report["commands"].append(entry)
        start = time.monotonic()
        result = subprocess.run(argv,cwd=ROOT,capture_output=True,text=True,
                                encoding="utf8",errors="replace",timeout=600)
        entry.update(exit_code=result.returncode,seconds=time.monotonic()-start,
                     stdout=result.stdout,stderr=result.stderr)
        if result.stdout: print(result.stdout,end="",flush=True)
        if result.stderr: print(result.stderr,end="",file=sys.stderr,flush=True)
        if result.returncode: raise RuntimeError(f"N08 command failed with exit{result.returncode}: {argv[0]}")
        return result.stdout

    try:
        report["preserved"] = preserved_inputs()
        report["preserved_count"] = len(report["preserved"])
        if not args.pins_only:
            names = ["ports/nds/core/time.c","ports/nds/core/events.c",
                     "ports/nds/core/contracts.c","ports/nds/core/memory.c",
                     "ports/nds/include/dok/time.h","ports/nds/include/dok/events.h",
                     "ports/nds/tests/time_cases.h","ports/nds/tests/time_cases.c",
                     "ports/nds/tests/test_time.c","ports/nds/tests/time_vectors.c",
                     "ports/nds/tests/events_vectors.c","ports/nds/tests/time_compile.c",
                     "ports/nds/tools/check_time.py","ports/nds/tools/run_time_emulator.py",
                     "ports/nds/tools/build_time.ps1","ports/nds/tools/audit_time.py",
                     "ports/nds/tools/audit_time_sdk.py",
                     "ports/nds/tools/export_time_evidence.ps1","ports/nds/Makefile.n08",
                     "tools/ghidra/ExportNdsTime.java"]
            for directory in ("ports/nds/platform/time", "ports/nds/platform/time_probe"):
                names.extend(path.relative_to(ROOT).as_posix() for path in (ROOT/directory).glob("*") if path.suffix in (".c",".h"))
            report["sources"] = [artifact(ROOT/name) for name in sorted(set(names))]
            version = run([str(args.zig),"version"]).strip()
            if version != "0.13.0": raise ValueError(f"Expected shared Zig0.13.0, got {version}")
            report["zig"] = dict(path=str(args.zig),version=version,sha256=digest(args.zig))
            run([sys.executable,"-B","-X","utf8","ports/nds/tools/audit_time.py"])
            run([sys.executable,"-B","-X","utf8","ports/nds/tools/audit_time_sdk.py"])
            core = ["ports/nds/core/time.c","ports/nds/core/events.c","ports/nds/core/contracts.c"]
            flags = ["-std=c11","-pedantic-errors","-Wall","-Wextra","-Werror","-O2","-g",
                     "-I","ports/nds/include","-I","ports/nds/tests","-I","ports/nds/platform/time_probe"]
            for name in ("test_time","time_vectors","events_vectors"):
                sources = core + (["ports/nds/tests/time_cases.c"] if name == "test_time" else []) + [f"ports/nds/tests/{name}.c"]
                for label,extra in ((".exe",[]),("-linux-x86_64",["-target","x86_64-linux-musl","-static"])):
                    output = safe_output(OUT/(name+label),OUT)
                    output.unlink(missing_ok=True)
                    run([str(args.zig),"cc",*flags,*extra,*sources,"-o",str(output)])
                    report["artifacts"].append(artifact(output))
                if name == "test_time": report["shared_suite"] = json.loads(run([str(OUT/"test_time.exe")]))
            for label,extra in (("native",[]),("linux-x86_64",["-target","x86_64-linux-musl"])):
                output = safe_output(OUT/f"time-compile-{label}.o",OUT)
                output.unlink(missing_ok=True)
                run([str(args.zig),"cc",*flags,*extra,"-c","ports/nds/tests/time_compile.c","-o",str(output)])
                report["artifacts"].append(artifact(output))
            report["numerical_vectors"] = numerical_vectors(OUT/"time_vectors.exe")
            report["original_vectors"] = original_vectors(OUT/"events_vectors.exe")
            if not args.host_only:
                run(["powershell","-NoProfile","-ExecutionPolicy","Bypass","-File",
                     "ports/nds/tools/build_time.ps1","-Rebuild"])
                build_path = OUT/"build-report.json"
                build = json.loads(build_path.read_text("utf-8-sig"))
                if not build.get("toolchain_lock_verified") or build.get("toolchain_lock_sha256") != digest(ROOT/"ports/nds/toolchain_versions.json"):
                    raise ValueError("N08 build did not verify accepted SDK package pins")
                expected = {f"build/nds/n08/dok_nds_time.{ext}" for ext in ("nds","elf","map")}
                if {row["path"] for row in build.get("artifacts",[])} != expected:
                    raise ValueError("Unexpected N08 ARM9 artifact set")
                for row in build["artifacts"]:
                    if artifact(ROOT/row["path"]) != row: raise ValueError("ARM9 artifact changed during gate")
                report["arm9_build"] = dict(report=artifact(build_path),artifacts=build["artifacts"])
            for row in report["sources"]:
                if artifact(ROOT/row["path"]) != row: raise ValueError(f"Source changed during N08 gate: {row['path']}")
            manifest = safe_output(EVIDENCE/"source-manifest.json",EVIDENCE)
            manifest.write_text(json.dumps(dict(task="N08",files=report["sources"]),indent=2)+"\n",encoding="utf8")
            report["source_manifest"] = artifact(manifest)
            build_manifest = safe_output(EVIDENCE/"build-manifest.json",EVIDENCE)
            build_manifest.write_text(json.dumps(dict(task="N08",artifacts=report["artifacts"]+report.get("arm9_build",{}).get("artifacts",[])),indent=2)+"\n",encoding="utf8")
            report["build_manifest"] = artifact(build_manifest)
        for row in report["preserved"]:
            if artifact(ROOT / row["path"]) != row:
                raise ValueError(f"Accepted input changed during audit: {row['path']}")
        report["passed"] = True
    except Exception as error:
        report["error"] = f"{type(error).__name__}: {error}"
        print(report["error"], file=sys.stderr)
    target.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf8")
    print(f"N08 {report['scope']}: {'PASS' if report['passed'] else 'FAIL'}; {target}")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
