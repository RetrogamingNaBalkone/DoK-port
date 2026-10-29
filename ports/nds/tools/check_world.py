#!/usr/bin/env python3
"""Build N05 strict C11 host gates, independent original oracle and ARM9 ROM.

Linux executables are linked, not run. Emulator execution is a separate gate.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import time

from audit_world import safe_output

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / "build/nds/n05/host"
EVIDENCE = ROOT / "research/evidence/nds/n05"
COMMON = ["ports/nds/core/assets.c", "ports/nds/core/sha256.c", "ports/nds/core/world.c", "ports/nds/core/balance.c"]


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    local = Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData/Local"))
    parser.add_argument("--zig", type=Path, default=local / "Programs/Zig/0.13.0/zig.exe")
    parser.add_argument("--package", type=Path, default=ROOT / "build/nds/assets/dok-assets.ndspak")
    parser.add_argument("--host-only", action="store_true")
    args = parser.parse_args()
    report_path = safe_output(EVIDENCE / "host-check.json", EVIDENCE)
    report = dict(schema_version=1, task="N05", passed=False, python=sys.version, host=platform.platform(),
                  scope="host_only" if args.host_only else "host_and_arm9_build", commands=[], sources=[], artifacts=[],
                  limitations=["Linux x86_64-musl binaries are compiled and linked, not executed.",
                               "ARM9 emulator execution is a separate gate; a successful build is not execution.",
                               "Initial seeds omit full native constructors, gameplay updates and events.",
                               "Physical Nintendo DS/DS Lite remains N24."])
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    def run(command: list[str], timeout: int = 300) -> str:
        record = dict(argv=command, cwd=str(ROOT))
        report["commands"].append(record)
        print("Running:", subprocess.list2cmdline(command), flush=True)
        start = time.monotonic()
        try:
            completed = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                                       encoding="utf-8", errors="replace", timeout=timeout)
        except subprocess.TimeoutExpired as exc:
            record.update(timed_out=True, seconds=time.monotonic() - start)
            raise RuntimeError(f"Command exceeded {timeout}s: {command[0]}") from exc
        record.update(exit_code=completed.returncode, seconds=time.monotonic() - start,
                      stdout=completed.stdout, stderr=completed.stderr)
        if completed.stdout:
            print(completed.stdout, end="", flush=True)
        if completed.stderr:
            print(completed.stderr, end="", file=sys.stderr, flush=True)
        if completed.returncode:
            raise RuntimeError(f"Command failed: exit {completed.returncode}")
        return completed.stdout

    try:
        version = run([str(args.zig), "version"]).strip()
        if version != "0.13.0":
            raise RuntimeError(f"Expected shared Zig 0.13.0, got {version}")
        report["zig"] = dict(path=str(args.zig), version=version, sha256=digest(args.zig))
        report["python_executable"] = dict(path=sys.executable, sha256=digest(Path(sys.executable)))
        package_before = digest(args.package)
        report["package"] = dict(path=str(args.package.resolve().relative_to(ROOT)).replace("\\", "/"),
                                 bytes=args.package.stat().st_size, sha256=package_before)
        extra = ["ports/nds/tests/test_world.c", "ports/nds/tests/test_balance.c", "ports/nds/tests/world_export.c",
                 "ports/nds/tests/world_export.h", "ports/nds/tests/world_dump.c", "ports/nds/tests/world_compile.c",
                 "ports/nds/tools/check_world.py", "ports/nds/tools/audit_world.py"]
        source_paths = set(COMMON + extra)
        source_paths.update(str(p.relative_to(ROOT)).replace("\\", "/") for p in (ROOT / "ports/nds/include").rglob("*.h"))
        for relative in sorted(source_paths):
            path = ROOT / relative
            report["sources"].append(dict(path=relative, bytes=path.stat().st_size, sha256=digest(path)))
        flags = ["-std=c11", "-pedantic-errors", "-Wall", "-Wextra", "-Werror", "-O2", "-g", "-I", "ports/nds/include", "-I", "ports/nds/tests"]
        targets = {
            "test_world": ["ports/nds/core/world.c", "ports/nds/core/sha256.c", "ports/nds/tests/test_world.c"],
            "test_balance": COMMON + ["ports/nds/tests/test_balance.c"],
            "world_dump": COMMON + ["ports/nds/tests/world_export.c", "ports/nds/tests/world_dump.c"],
        }
        native_outputs = {}
        for name, sources in targets.items():
            native = safe_output(OUT / f"{name}.exe", OUT)
            linux = safe_output(OUT / f"{name}-linux-x86_64", OUT)
            for path in (native, linux):
                path.unlink(missing_ok=True)
            run([str(args.zig), "cc", *flags, *sources, "-o", str(native)])
            native_outputs[name] = native
            if name != "world_dump":
                stdout = run([str(native), *([str(args.package)] if name == "test_balance" else [])])
                try:
                    report[name] = json.loads(stdout)
                except json.JSONDecodeError:
                    report[name] = dict(successful_exit=True, stdout=stdout)
            run([str(args.zig), "cc", *flags, "-target", "x86_64-linux-musl", "-static", *sources, "-o", str(linux)])
            for path in (native, linux):
                report["artifacts"].append(dict(path=str(path.relative_to(ROOT)).replace("\\", "/"), bytes=path.stat().st_size, sha256=digest(path)))
        for name, target in (("native", []), ("linux-x86_64", ["-target", "x86_64-linux-musl"])):
            output = safe_output(OUT / f"world-compile-{name}.o", OUT)
            output.unlink(missing_ok=True)
            run([str(args.zig), "cc", *flags, *target, "-c", "ports/nds/tests/world_compile.c", "-o", str(output)])
            report["artifacts"].append(dict(path=str(output.relative_to(ROOT)).replace("\\", "/"), bytes=output.stat().st_size, sha256=digest(output)))
        run([sys.executable, "-X", "utf8", "ports/nds/tools/audit_world.py", "--package", str(args.package), "--exporter", str(native_outputs["world_dump"])])
        oracle_path = EVIDENCE / "oracle-report.json"
        oracle = json.loads(oracle_path.read_text(encoding="utf-8"))
        if not oracle.get("passed") or len(oracle.get("cases", [])) != 6 or not all(c["checked"] for c in oracle["cases"]):
            raise RuntimeError("Independent original oracle did not pass all six canonical cases")
        report["oracle"] = dict(path=str(oracle_path.relative_to(ROOT)).replace("\\", "/"), sha256=digest(oracle_path), cases=oracle["cases"])
        if not args.host_only:
            run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "ports/nds/tools/build_world.ps1", "-Rebuild"])
            build_path = ROOT / "build/nds/n05/build-report.json"
            build = json.loads(build_path.read_text(encoding="utf-8-sig"))
            if not build.get("toolchain_lock_verified") or build["toolchain_lock_sha256"] != digest(ROOT / "ports/nds/toolchain_versions.json"):
                raise RuntimeError("ARM9 builder did not verify the actual pinned SDK")
            expected = {f"build/nds/n05/dok_nds_world.{ext}" for ext in ("nds", "elf", "map")}
            if {row["path"] for row in build.get("artifacts", [])} != expected:
                raise RuntimeError("Unexpected ARM9 artifact set")
            for row in build["artifacts"]:
                path = ROOT / row["path"]
                if path.stat().st_size != row["bytes"] or digest(path) != row["sha256"]:
                    raise RuntimeError(f"ARM9 artifact changed: {row['path']}")
            report["arm9_build"] = dict(path=str(build_path.relative_to(ROOT)).replace("\\", "/"), sha256=digest(build_path), artifacts=build["artifacts"])
        if digest(args.package) != package_before:
            raise RuntimeError("Package changed during validation")
        for source in report["sources"]:
            if digest(ROOT / source["path"]) != source["sha256"]:
                raise RuntimeError(f"Source changed during validation: {source['path']}")
        report["passed"] = True
    except Exception as exc:
        report["error"] = f"{type(exc).__name__}: {exc}"
        print(report["error"], file=sys.stderr, flush=True)
    finally:
        report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print(f"N05 host/build passed={report['passed']}; {report_path}", flush=True)
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
